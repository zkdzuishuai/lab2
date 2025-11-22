#include "../../include/passes/FunctionInline.hpp"
#include "../../include/lightir/Function.hpp"

#include "BasicBlock.hpp"
#include "Instruction.hpp"
#include "Value.hpp"
#include "Module.hpp"
#include "logging.hpp"

#include <cassert>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include <string>

void FunctionInline::run() {
    inline_all_functions();
}

void FunctionInline::inline_all_functions() {
    // 1. 收集递归函数
    std::set<Function *> recursive_func;
    for (auto &func : m_->get_functions()) {
        for (auto &bb : func.get_basic_blocks()) {
            for (auto &inst : bb.get_instructions()) {
                if (!inst.is_call())
                    continue;
                auto *call   = static_cast<CallInst *>(&inst);
                auto *callee = static_cast<Function *>(call->get_operand(0));
                if (callee == &func) {
                    recursive_func.insert(callee);
                    break;
                }
            }
        }
    }

    // 2. 对每个函数做内联
    for (auto &func_ref : m_->get_functions()) {
        auto *func = &func_ref;

        // 跳过外部 I/O 函数本身
        if (outside_func.count(func->get_name()))
            continue;

    a1:
        for (auto &bb_ref : func->get_basic_blocks()) {
            auto *bb = &bb_ref;
            for (auto &inst_ref : bb->get_instructions()) {
                auto *inst = &inst_ref;
                if (!inst->is_call())
                    continue;

                auto *call   = static_cast<CallInst *>(inst);
                auto *callee = static_cast<Function *>(call->get_operand(0));

                // 不内联递归调用到自身
                if (callee == func)
                    continue;

                // 不内联递归函数
                if (recursive_func.count(callee))
                    continue;

                // 不内联外部 I/O 函数
                if (outside_func.count(callee->get_name()))
                    continue;

                // 过大的函数先不内联，避免代码膨胀
                if (callee->get_basic_blocks().size() >= 6)
                    continue;

                // 真正进行一次内联，然后重新从头扫本函数
                inline_function(call, callee);
                goto a1;
            }
        }
    }
}

void FunctionInline::inline_function(Instruction *call_inst, Function *origin) {
    auto *call      = static_cast<CallInst *>(call_inst);
    auto *call_bb   = call->get_parent();
    auto *call_func = call_bb->get_parent();
    auto *module    = call_func->get_parent();

    // 1. 形参与实参映射
    std::map<Value *, Value *> v_map;
    // 计算形参数量，用于安全检查
    unsigned formal_cnt = 0;
    for (auto &arg : origin->get_args()) {
        ++formal_cnt;
    }

    // call 的 operand[0] 是函数本身，从1开始是实参
    unsigned total_ops = call->get_num_operand();
    if (total_ops < 1 + formal_cnt) {
        // 参数数量不匹配，保守起见直接不做内联，避免越界访问
        return;
    }

    unsigned arg_idx = 1;
    for (auto &arg : origin->get_args()) {
        v_map[&arg] = call->get_operand(arg_idx++);
    }

    // 2. 为 origin 的每个基本块创建一个新的基本块
    std::vector<BasicBlock *> new_bbs;
    new_bbs.reserve(origin->get_basic_blocks().size());
    for (auto &bb_ref : origin->get_basic_blocks()) {
        auto *bb     = &bb_ref;
        auto *bb_new = BasicBlock::create(module, "", call_func);
        v_map[bb]    = bb_new;
        new_bbs.push_back(bb_new);
    }

    // 3. 克隆指令
    std::vector<Instruction *> ret_list;      // 非 void 返回指令列表
    std::vector<BasicBlock *>  ret_void_bbs;  // void 函数的返回基本块列表

    auto bb_it    = origin->get_basic_blocks().begin();
    auto newbb_it = new_bbs.begin();
    for (; bb_it != origin->get_basic_blocks().end(); ++bb_it, ++newbb_it) {
        auto &bb     = *bb_it;
        auto *bb_new = *newbb_it;

        for (auto &inst_ref : bb.get_instructions()) {
            auto *inst = &inst_ref;

            // void 函数：记录返回所在new_bb，稍后统一接到bb_after_call
            if (inst->is_ret() && origin->get_return_type()->is_void_type()) {
                ret_void_bbs.push_back(bb_new);
                continue;
            }

            Instruction *inst_new = nullptr;

            if (inst->is_phi()) {
                inst_new = inst->clone(bb_new);
            } else if (inst->is_call()) {
                auto *inner_call = static_cast<CallInst *>(inst);
                auto *callee     = static_cast<Function *>(inner_call->get_operand(0));
                std::vector<Value *> args(
                    inner_call->get_operands().begin() + 1,
                    inner_call->get_operands().end()
                );
                inst_new = CallInst::create_call(callee, args, bb_new);
            } else {
                inst_new = inst->clone(bb_new);
            }

            v_map[inst] = inst_new;

            if (inst->is_ret() && !origin->get_return_type()->is_void_type()) {
                ret_list.push_back(inst_new);
            }
        }
    }

    // 4. 替换新指令里的操作数（根据 v_map）
    for (auto *bb_new : new_bbs) {
        for (auto &inst_ref : bb_new->get_instructions()) {
            auto *inst = &inst_ref;
            for (unsigned i = 0; i < inst->get_num_operand(); ++i) {
                auto *op = inst->get_operand(i);
                auto it  = v_map.find(op);
                if (it != v_map.end()) {
                    inst->set_operand(i, it->second);
                }
            }
        }
    }

    // 5. 处理返回值 / 返回控制流
    Value *ret_val       = nullptr;            // 非 void 函数的统一返回值
    auto  *bb_after_call = BasicBlock::create(module, "", call_func);

    if (!origin->get_return_type()->is_void_type()) {
        if (ret_list.size() == 1) {
            // 单个 return：直接把返回块跳到 bb_after_call
            auto *ret    = ret_list.front();
            ret_val      = ret->get_operand(0);
            auto *ret_bb = ret->get_parent();

            ret_bb->remove_instr(ret);
            BranchInst::create_br(bb_after_call, ret_bb);
        } else if (!ret_list.empty()) {
            // 多个 return：建 bb_phi + phi 汇总返回值
            auto *bb_phi = BasicBlock::create(module, "", call_func);

            std::vector<Value *>      phi_vals;
            std::vector<BasicBlock *> phi_bbs;
            phi_vals.reserve(ret_list.size());
            phi_bbs.reserve(ret_list.size());

            for (auto *ret_inst : ret_list) {
                auto *ret_bb = ret_inst->get_parent();
                phi_vals.push_back(ret_inst->get_operand(0));
                phi_bbs.push_back(ret_bb);

                ret_bb->remove_instr(ret_inst);
                BranchInst::create_br(bb_phi, ret_bb);
            }

            auto *phi = PhiInst::create_phi(origin->get_return_type(),
                                            bb_phi, phi_vals, phi_bbs);
            ret_val = phi;

            BranchInst::create_br(bb_after_call, bb_phi);
            new_bbs.push_back(bb_phi);
        }
        // 如果 ret_list 为空而类型非 void，其实是非法 IR，这里就不特判了
    } else {
        // void 函数：所有返回块直接跳到 bb_after_call
        assert(!ret_void_bbs.empty() && "void function must have at least one return");
        for (auto *bb_ret : ret_void_bbs) {
            BranchInst::create_br(bb_after_call, bb_ret);
        }
    }

    // 6. 处理调用点所在基本块：把 call 之后的指令搬到 bb_after_call
    std::vector<Instruction *> after_call;
    bool                       seen_call = false;
    for (auto &inst_ref : call_bb->get_instructions()) {
        auto *inst = &inst_ref;
        if (!seen_call) {
            if (inst == call) {
                seen_call = true;
            }
        } else {
            after_call.push_back(inst);
        }
    }

    // 把这些指令移到 bb_after_call
    for (auto *inst : after_call) {
        call_bb->remove_instr(inst);
        bb_after_call->add_instruction(inst);
        inst->set_parent(bb_after_call);
    }

    // 先处理call本身的返回值替换，再删掉call
    if (!origin->get_return_type()->is_void_type() && ret_val) {
        call->replace_all_use_with(ret_val);
    }
    call_bb->remove_instr(call);

    // 到这里，call_bb已经不再有terminator，可以安全插入新的br
    auto *entry_bb = new_bbs.front();
    BranchInst::create_br(entry_bb, call_bb);
}