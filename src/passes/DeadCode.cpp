#include "DeadCode.hpp"
#include "Instruction.hpp"
#include "logging.hpp"

#include <deque>
#include <unordered_set>
#include <vector>
#include <memory>

void DeadCode::run() {
    // 虽然当前实现没有用到FuncInfo的纯函数信息，但让它先跑一遍
    func_info->run();

    bool changed;
    do {
        changed = false;
        for (auto &f : m_->get_functions()) {
            if (f.is_declaration())
                continue;

            auto func = &f;
            // 对每个函数做一轮mark / sweep，如果删掉了指令就再来一轮
            mark(func);
            if (sweep(func)) {
                changed = true;
            }
        }
    } while (changed);

    LOG_INFO << "dead code pass erased " << ins_count << " instructions";
}

bool DeadCode::clear_basic_blocks(Function *func) {
    // 这里暂时不做不可达基本块删除，避免破坏CFG造成段错误。
    // (void)func;
    // return false;
   bool changed = 0;
    std::vector<BasicBlock *> to_erase;
    for (auto &bb1 : func->get_basic_blocks()) {
        auto bb = &bb1;
        if(bb->get_pre_basic_blocks().empty() && bb != func->get_entry_block()) {
            to_erase.push_back(bb);
            changed = 1;
        }
    }
    for (auto &bb : to_erase) {
        bb->erase_from_parent();
        delete bb;
    }
    return changed;
}

void DeadCode::mark(Function *func) {
    // 标记阶段：从关键指令出发，反向沿def-use链传播“存活”标记

    // 每次处理一个函数时清空工作队列与标记表
    work_list.clear();
    marked.clear();

    // 先把所有关键指令（有副作用 / 影响控制流）放入工作队列
    for (auto &bb : func->get_basic_blocks()) {
        for (auto &ins_ref : bb.get_instructions()) {
            auto *inst = &ins_ref;

            if (is_critical(inst)) {
                if (!marked[inst]) {          // 默认值是false，这里只在第一次标记时入队
                    marked[inst] = true;
                    work_list.push_back(inst);
                }
            } else {
                // 非关键指令先默认记为“未存活”
                marked[inst] = false;
            }
        }
    }

    // 通过工作队列沿着 def-use 链向前传播
    while (!work_list.empty()) {
        auto *cur = work_list.front();
        work_list.pop_front();
        mark(cur);
    }
}

void DeadCode::mark(Instruction *ins) {
    // 对当前指令使用到的每一个操作数，找到其“定义指令”，并将其标记为存活
    for (auto *op : ins->get_operands()) {
        auto *def = dynamic_cast<Instruction *>(op);
        if (def == nullptr)
            continue;

        // 只在同一函数内追踪，防止跨函数乱窜
        if (def->get_function() != ins->get_function())
            continue;

        if (marked[def])      // 已经标记过就不用再入队
            continue;

        marked[def] = true;
        work_list.push_back(def);
    }
}

bool DeadCode::sweep(Function *func) {
    // 删除阶段：清除所有未被标记为存活、且无副作用的指令
    std::unordered_set<Instruction *> wait_del{};

    // 1. 收集所有“未被标记为存活”的指令
    for (auto &bb_ref : func->get_basic_blocks()) {
        auto *bb = &bb_ref;
        for (auto &ins_ref : bb->get_instructions()) {
            auto *inst = &ins_ref;

            // 保险起见，对关键指令再过滤一次
            if (is_critical(inst))
                continue;

            // 如果没有在marked中被标记为true，就认为是死代码
            if (!(marked.count(inst) && marked[inst])) {
                wait_del.insert(inst);
            }
        }
    }

    // 2. 统一删除，避免在遍历链表时修改链表结构
    for (auto *inst : wait_del) {
        auto *bb = inst->get_parent();
        bb->erase_instr(inst);     // BasicBlock 已经提供安全删除接口
    }

    ins_count += static_cast<int>(wait_del.size());
    return !wait_del.empty();      // 是否有改动
}

bool DeadCode::is_critical(Instruction *ins) {
    // 判断指令是否“关键”：

    // 1. 控制流相关：ret / br
    if (ins->is_br() || ins->is_ret())
        return true;

    // 2. 写内存：store 有副作用
    if (ins->is_store())
        return true;

    // 3. 函数调用
    if (ins->is_call()) {
        auto *call = static_cast<CallInst *>(ins);
        auto *callee = dynamic_cast<Function *>(call->get_operand(0));

        // 如果知道这是一个纯函数（FuncInfo 分析得到），
        // 那调用本身没有副作用，只要返回值没人用就可以删。
        // 因此只有“非纯函数调用”被视为关键。
        if (callee && func_info->is_pure_function(callee)) {
            return false;
        }
        return true;
    }

    // 其它算术 / load / phi / gep 等指令是否保留，
    // 完全由它们是否被关键指令间接使用来决定，这里都视为“非关键”。
    return false;
}

void DeadCode::sweep_globally() {
    // 不手动 delete，由 Module 自己管理生命周期。
    std::vector<Function *> unused_funcs;
    std::vector<GlobalVariable *> unused_globals;

    for (auto &f_r : m_->get_functions()) {
        if (f_r.get_use_list().empty() && f_r.get_name() != "main")
            unused_funcs.push_back(&f_r);
    }

    for (auto &glob_var_r : m_->get_global_variable()) {
        if (glob_var_r.get_use_list().empty())
            unused_globals.push_back(&glob_var_r);
    }

    for (auto *func : unused_funcs) {
        m_->get_functions().erase(func);
    }
    for (auto *glob : unused_globals) {
        m_->get_global_variable().erase(glob);
    }
}
