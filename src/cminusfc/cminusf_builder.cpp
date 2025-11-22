#include "cminusf_builder.hpp"
#include <llvm/IR/GlobalValue.h>

#define CONST_FP(num) ConstantFP::get((float)num, module.get())
#define CONST_INT(num) ConstantInt::get(num, module.get())

// types
Type *VOID_T;
Type *INT1_T;
Type *INT32_T;
Type *INT32PTR_T;
Type *FLOAT_T;
Type *FLOATPTR_T;

bool promote(IRBuilder *builder, Value **l_val_p, Value **r_val_p) {
    bool is_int = false;
    auto &l_val = *l_val_p;
    auto &r_val = *r_val_p;
    if (l_val->get_type() == r_val->get_type()) {
        is_int = l_val->get_type()->is_integer_type();
    } else {
        if (l_val->get_type()->is_integer_type()) {
            l_val = builder->create_sitofp(l_val, FLOAT_T);
        } else {
            r_val = builder->create_sitofp(r_val, FLOAT_T);
        }
    }
    return is_int;
}

/*
 * use CMinusfBuilder::Scope to construct scopes
 * scope.enter: enter a new scope
 * scope.exit: exit current scope
 * scope.push: add a new binding to current scope
 * scope.find: find and return the value bound to the name
 */

Value* CminusfBuilder::visit(ASTProgram &node) {
    VOID_T = module->get_void_type();
    INT1_T = module->get_int1_type();
    INT32_T = module->get_int32_type();
    INT32PTR_T = module->get_int32_ptr_type();
    FLOAT_T = module->get_float_type();
    FLOATPTR_T = module->get_float_ptr_type();

    Value *ret_val = nullptr;
    for (auto &decl : node.declarations) {
        ret_val = decl->accept(*this);
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTNum &node) {
    if (node.type == TYPE_INT) {
        return CONST_INT(node.i_val);
    }
    return CONST_FP(node.f_val);
}

Value* CminusfBuilder::visit(ASTVarDeclaration &node) {
    // TODO: This function is empty now.
    // Add some code here.
   // 判断变量名是否为空
if (node.id.empty()) {
    std::cerr << "Error: Variable name is empty!" << std::endl;
    return nullptr;
}
// 取出元素类型
Type *elem_type = (node.type == TYPE_INT) ? INT32_T : FLOAT_T;

Value *var_val = nullptr;
if (node.num) {
        // 数组声明
        int array_size = node.num->i_val;
        auto *array_type = ArrayType::get(elem_type, array_size);

        if (scope.in_global()) {
            // 全局数组：用 ConstantZero 初始化
            auto *init_arr = ConstantZero::get(array_type, module.get());
            var_val = GlobalVariable::create(node.id, module.get(), array_type, false, init_arr);
        } else {
            // 局部数组：在栈上分配
            var_val = builder->create_alloca(array_type);
        }
    } else {
        // 标量声明
        if (scope.in_global()) {
            // 全局标量：用 ConstantZero 初始化，避免 init_val_ 为 nullptr
            auto *init_val = ConstantZero::get(elem_type, module.get());
            var_val = GlobalVariable::create(node.id, module.get(), elem_type, false, init_val);
        } else {
            // 局部标量：在栈上分配并初始化为 0
            var_val = builder->create_alloca(elem_type);
            if (elem_type == INT32_T)
                builder->create_store(CONST_INT(0), var_val);
            else
                builder->create_store(CONST_FP(0.0f), var_val);
        }
    }

    scope.push(node.id, var_val);
    return var_val;
    //return nullptr;
}

Value* CminusfBuilder::visit(ASTFunDeclaration &node) {
    FunctionType *fun_type;
    Type *ret_type;
    std::vector<Type *> param_types;
    if (node.type == TYPE_INT)
        ret_type = INT32_T;
    else if (node.type == TYPE_FLOAT)
        ret_type = FLOAT_T;
    else
        ret_type = VOID_T;

    for (auto &param : node.params) {
        if (param->type == TYPE_INT) {
            if (param->isarray) {
                param_types.push_back(INT32PTR_T);
            } else {
                param_types.push_back(INT32_T);
            }
        } else {
            if (param->isarray) {
                param_types.push_back(FLOATPTR_T);
            } else {
                param_types.push_back(FLOAT_T);
            }
        }
    }

    fun_type = FunctionType::get(ret_type, param_types);
    auto func = Function::create(fun_type, node.id, module.get());
    scope.push(node.id, func);
    context.func = func;

    idx_count = 0;
    if_count    = 0;
    while_count = 0;
    
    auto funBB = BasicBlock::create(module.get(), "entry", func);
    builder->set_insert_point(funBB);
    scope.enter();
    context.pre_enter_scope = true;
    std::vector<Value *> args;
    for (auto &arg : func->get_args()) {
        args.push_back(&arg);
    }
    for (unsigned int i = 0; i < node.params.size(); ++i) {
        auto* param_i = node.params[i]->accept(*this);
        args[i]->set_name(node.params[i]->id);
        builder->create_store(args[i], param_i);
        scope.push(args[i]->get_name(), param_i);
    }
    node.compound_stmt->accept(*this);
    auto *bb = builder->get_insert_block();
if (bb && !bb->is_terminated()) {
    if (ret_type == VOID_T)       builder->create_void_ret();
    else if (ret_type == INT32_T) builder->create_ret(CONST_INT(0));
    else if (ret_type == FLOAT_T) builder->create_ret(CONST_FP(0.0f));
}
    scope.exit();
    return nullptr;
}

Value* CminusfBuilder::visit(ASTParam &node) {
    Type *param_type = nullptr;
if (node.type == TYPE_INT) {
    param_type = node.isarray ? INT32PTR_T : INT32_T;
} else {
    param_type = node.isarray ? FLOATPTR_T : FLOAT_T;
}
// 在栈上分配参数空间
auto *alloca = builder->create_alloca(param_type);
return alloca;
}

Value* CminusfBuilder::visit(ASTCompoundStmt &node) {
    // TODO: This function is not complete.
    // You may need to add some code here
    // to deal with complex statements. 
    scope.enter();
    for (auto &decl : node.local_declarations) {
        decl->accept(*this);
    }

    for (auto &stmt : node.statement_list) {
    stmt->accept(*this);
    auto *bb = builder->get_insert_block();
    if (bb && bb->is_terminated())  //只有在块已终止时才跳出
        break;
}

    scope.exit();
    return nullptr;
}

Value* CminusfBuilder::visit(ASTExpressionStmt &node) {
    if (node.expression != nullptr) {
        return node.expression->accept(*this);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTSelectionStmt &node) {
    // 1) 条件 -> i1
    Value *cond = node.expression->accept(*this);
    if (cond->get_type() == INT32_T)       cond = builder->create_icmp_ne(cond, CONST_INT(0));
    else if (cond->get_type() == FLOAT_T)  cond = builder->create_fcmp_ne(cond, CONST_FP(0.0f));

    auto *func   = context.func;
    
    // 生成唯一块名并自增计数器
    std::string then_name = "if.then." + std::to_string(if_count);
    std::string else_name = "if.else." + std::to_string(if_count);
    std::string cont_name = "if.cont." + std::to_string(if_count);
    if_count++;

    auto *thenBB = BasicBlock::create(module.get(), then_name, func);
    BasicBlock *elseBB = nullptr;
    auto *contBB = BasicBlock::create(module.get(), cont_name, func);

    // 根据是否存在 else 语句创建条件跳转
    if (node.else_statement) {
        elseBB = BasicBlock::create(module.get(), else_name, func);
        builder->create_cond_br(cond, thenBB, elseBB);
    } else {
        builder->create_cond_br(cond, thenBB, contBB);
    }

    // 2) then 分支
    builder->set_insert_point(thenBB);
    node.if_statement->accept(*this);  // 递归处理 then 分支
    {
        // 使用当前插入块 currBB 而非 thenBB 判断
        auto *currBB = builder->get_insert_block();
        if (currBB && !currBB->is_terminated()) {
            builder->create_br(contBB);
        }
    }

    // 3) else 分支（如果有）
    if (node.else_statement) {
        builder->set_insert_point(elseBB);
        node.else_statement->accept(*this);  // 递归处理 else 分支
        auto *currBB = builder->get_insert_block();
        if (currBB && !currBB->is_terminated()) {
            builder->create_br(contBB);
        }
    }
    
    // 4) 最后确保插入点切换到 contBB
    builder->set_insert_point(contBB);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTIterationStmt &node) {
    // TODO: This function is empty now.
    // Add some code here.
    auto *func   = context.func;
    auto *preBB  = builder->get_insert_block();
    
    // 生成唯一的 while 块名并自增计数器
    std::string cond_name = "while.cond." + std::to_string(while_count);
    std::string body_name = "while.body." + std::to_string(while_count);
    std::string exit_name = "while.exit." + std::to_string(while_count);
    while_count++;

    auto *condBB = BasicBlock::create(module.get(), cond_name, func);
    auto *bodyBB = BasicBlock::create(module.get(), body_name, func);
    auto *exitBB = BasicBlock::create(module.get(), exit_name, func);

    // 从当前块跳到条件块（当前块若未终止）
    if (preBB && !preBB->is_terminated()) {
        builder->create_br(condBB);
    }

    // 条件块
    builder->set_insert_point(condBB);
    Value *cond = node.expression->accept(*this);
    if (cond->get_type() == INT32_T) {
        cond = builder->create_icmp_ne(cond, CONST_INT(0));
    } else if (cond->get_type() == FLOAT_T) {
        cond = builder->create_fcmp_ne(cond, CONST_FP(0.0f));
    }
    builder->create_cond_br(cond, bodyBB, exitBB);

    // 循环体
    builder->set_insert_point(bodyBB);
    node.statement->accept(*this);
    {
        auto *currBB = builder->get_insert_block();
        // 如果当前基本块还未终结，则跳回条件块
        if (currBB && !currBB->is_terminated()) {
            builder->create_br(condBB);
        }
    }

    // 出口作为后续插入点
    builder->set_insert_point(exitBB);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTReturnStmt &node) {
    if (node.expression == nullptr) {
        builder->create_void_ret();
    } else {
        Type *fun_ret_type = context.func->get_function_type()->get_return_type();
        Value *ret_val = node.expression->accept(*this);

        if (fun_ret_type != ret_val->get_type()) {
            // 函数返回整型
            if (fun_ret_type->is_integer_type()) {
                // 返回值是浮点，使用 fptosi
                if (ret_val->get_type()->is_float_type()) {
                    ret_val = builder->create_fptosi(ret_val, INT32_T);
                }
                // 返回值是布尔，使用 zext
                else if (ret_val->get_type()->is_int1_type()) {
                    ret_val = builder->create_zext(ret_val, INT32_T);
                }
            }
            // 函数返回浮点型
            else if (fun_ret_type->is_float_type()) {
                // 整型或布尔转成浮点
                if (ret_val->get_type()->is_integer_type()) {
                    ret_val = builder->create_sitofp(ret_val, FLOAT_T);
                }
            }
        }

        builder->create_ret(ret_val);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTVar &node) {
    Value* baseAddr = this->scope.find(node.id);
    if (!baseAddr) {
        return nullptr; // scope.find 已输出错误信息
    }

    Type* alloctype = nullptr;
    
    if(baseAddr->is<AllocaInst>()) {
        alloctype = baseAddr->as<AllocaInst>()->get_alloca_type();
    } else if (baseAddr->is<GlobalVariable>()) {
        alloctype = baseAddr->as<GlobalVariable>()->get_type()->get_pointer_element_type();
    } else {
        std::cerr << "Error: variable '" << node.id << "' has unsupported base type." << std::endl;
        return nullptr;
    }

    if(node.expression) {
        // 先计算下标并转成 i32
        bool orig_lv = context.require_lvalue;
        context.require_lvalue = false;
        auto idx = node.expression->accept(*this);
        context.require_lvalue = orig_lv;
        if (idx->get_type()->is_float_type())
            idx = builder->create_fptosi(idx, INT32_T);
        else if (idx->get_type()->is_int1_type())
            idx = builder->create_zext(idx, INT32_T);

        // 创建 okBB / negBB
        auto *func = context.func;
        std::string ok_name  = "idx.ok." + std::to_string(idx_count);
        std::string neg_name = "idx.neg." + std::to_string(idx_count);
        idx_count++;

        auto *okBB  = BasicBlock::create(module.get(), ok_name, func);
        auto *negBB = BasicBlock::create(module.get(), neg_name, func);
        auto cond_neg = builder->create_icmp_ge(idx, CONST_INT(0));
        builder->create_cond_br(cond_neg, okBB, negBB);

        // 负下标分支：调用 neg_idx_except() 并回跳到 okBB
        builder->set_insert_point(negBB);
        auto wrong = scope.find("neg_idx_except");
        if (wrong) {
            builder->create_call(wrong, {});
        } else {
            std::cerr << "Error: function 'neg_idx_except' not found." << std::endl;
        }
        builder->create_br(okBB);

        // 正常分支
        builder->set_insert_point(okBB);
        if (context.require_lvalue) {
            if (alloctype->is_pointer_type()) {
                baseAddr = builder->create_load(baseAddr);
                baseAddr = builder->create_gep(baseAddr, {idx});
            } else if (alloctype->is_array_type()) {
                baseAddr = builder->create_gep(baseAddr, {CONST_INT(0), idx});
            }
            context.require_lvalue = false;
            return baseAddr;
        } else {
            if (alloctype->is_pointer_type()) {
                baseAddr = builder->create_load(baseAddr);
                baseAddr = builder->create_gep(baseAddr, {idx});
            } else if (alloctype->is_array_type()) {
                baseAddr = builder->create_gep(baseAddr, {CONST_INT(0), idx});
            }
            return builder->create_load(baseAddr);
        }
    } else {
        // 无下标：访问标量或数组名
        if (context.require_lvalue) {
            context.require_lvalue = false;
            return baseAddr;
        } else {
            if (alloctype->is_array_type()) {
                // 数组名作为右值：返回数组首元素地址
                return builder->create_gep(baseAddr, {CONST_INT(0), CONST_INT(0)});
            } else {
                return builder->create_load(baseAddr);
            }
        }
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTAssignExpression &node) {
    // 计算右值
    Value *expr_result = node.expression->accept(*this);
    // 获取左值地址
    context.require_lvalue = true;
    Value *var_addr = node.var->accept(*this);
    context.require_lvalue = false;

    Type *lhs_ty = var_addr->get_type()->get_pointer_element_type();
    Type *rhs_ty = expr_result->get_type();

    if (lhs_ty != rhs_ty) {
        // 左值是浮点，右值是整数或布尔
        if (lhs_ty->is_float_type() && rhs_ty->is_integer_type()) {
            if (rhs_ty == INT1_T) {
                // 布尔先转成 i32
                expr_result = builder->create_zext(expr_result, INT32_T);
            }
            // 整数转成浮点
            expr_result = builder->create_sitofp(expr_result, FLOAT_T);
        }
        // 左值是整数，右值是浮点
        else if (lhs_ty->is_integer_type() && rhs_ty->is_float_type()) {
            expr_result = builder->create_fptosi(expr_result, INT32_T);
        }
        // 左值是整数，右值是布尔
        else if (lhs_ty->is_integer_type() && rhs_ty == INT1_T) {
            expr_result = builder->create_zext(expr_result, INT32_T);
        }
    }

    // 存储转换后的值
    builder->create_store(expr_result, var_addr);
    return expr_result;
}

Value* CminusfBuilder::visit(ASTSimpleExpression &node) {
    // TODO: This function is empty now.
    // Add some code here.
    // Generate IR for the left and right expressions
    // 左边必有
    Value *lhs = node.additive_expression_l->accept(*this);

    // 没有比较，直接返回左侧结果
    if (node.additive_expression_r == nullptr) {
        return lhs;
    }

    // 有比较：算出右边
    Value *rhs = node.additive_expression_r->accept(*this);

    // 关键统一提升到同一类型 
    auto *LT = lhs->get_type();
    auto *RT = rhs->get_type();

    if (LT->is_float_type() || RT->is_float_type()) {
        // 任一为 float，则两边都转成 float，再用 fcmp_*
        if (LT->is_integer_type()) lhs = builder->create_sitofp(lhs, FLOAT_T);
        if (RT->is_integer_type()) rhs = builder->create_sitofp(rhs, FLOAT_T);

        switch (node.op) {
            case OP_EQ:  return builder->create_fcmp_eq(lhs, rhs);
            case OP_NEQ: return builder->create_fcmp_ne(lhs, rhs);
            case OP_LT:  return builder->create_fcmp_lt(lhs, rhs);
            case OP_LE:  return builder->create_fcmp_le(lhs, rhs);
            case OP_GT:  return builder->create_fcmp_gt(lhs, rhs);
            case OP_GE:  return builder->create_fcmp_ge(lhs, rhs);
        }
    } else {
        // LightIR 的 icmp 要求两边都是 i32；如果碰到 i1，先 zext 到 i32
    if (lhs->get_type() == INT1_T) {
        lhs = builder->create_zext(lhs, INT32_T);
    }
    if (rhs->get_type() == INT1_T) {
        rhs = builder->create_zext(rhs, INT32_T);
    }
    // （防御）如果意外混入了 float，这里也强制转成 i32 再比较
    if (lhs->get_type()->is_float_type()) {
        lhs = builder->create_fptosi(lhs, INT32_T);
    }
    if (rhs->get_type()->is_float_type()) {
        rhs = builder->create_fptosi(rhs, INT32_T);
    }

    switch (node.op) {
        case OP_EQ:  return builder->create_icmp_eq(lhs, rhs);
        case OP_NEQ: return builder->create_icmp_ne(lhs, rhs);
        case OP_LT:  return builder->create_icmp_lt(lhs, rhs);
        case OP_LE:  return builder->create_icmp_le(lhs, rhs);
        case OP_GT:  return builder->create_icmp_gt(lhs, rhs);
        case OP_GE:  return builder->create_icmp_ge(lhs, rhs);
    }
    }
    return nullptr;
    //return nullptr;
}

Value* CminusfBuilder::visit(ASTAdditiveExpression &node) {
    if (node.additive_expression == nullptr) {
        return node.term->accept(*this);
    }

    auto *l_val = node.additive_expression->accept(*this);
    auto *r_val = node.term->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);
    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_PLUS:
        if (is_int) {
            ret_val = builder->create_iadd(l_val, r_val);
        } else {
            ret_val = builder->create_fadd(l_val, r_val);
        }
        break;
    case OP_MINUS:
        if (is_int) {
            ret_val = builder->create_isub(l_val, r_val);
        } else {
            ret_val = builder->create_fsub(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTTerm &node) {
    if (node.term == nullptr) {
        return node.factor->accept(*this);
    }

    auto *l_val = node.term->accept(*this);
    auto *r_val = node.factor->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);

    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_MUL:
        if (is_int) {
            ret_val = builder->create_imul(l_val, r_val);
        } else {
            ret_val = builder->create_fmul(l_val, r_val);
        }
        break;
    case OP_DIV:
        if (is_int) {
            ret_val = builder->create_isdiv(l_val, r_val);
        } else {
            ret_val = builder->create_fdiv(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTCall &node) {
    auto *callee = static_cast<Function *>(scope.find(node.id));
    std::vector<Value *> args;
    auto param_ty_it = callee->get_function_type()->param_begin();

    for (auto &arg : node.args) {
        auto *v     = arg->accept(*this);
        auto *need  = *param_ty_it;
        auto *have  = v->get_type();

        // i1 -> i32
        if (have == INT1_T && need == INT32_T) {
            v = builder->create_zext(v, INT32_T);
        }
        // i32 -> float
        else if (have == INT32_T && need->is_float_type()) {
            v = builder->create_sitofp(v, FLOAT_T);
        }
        // float -> i32
        else if (have == FLOAT_T && need->is_integer_type()) {
            v = builder->create_fptosi(v, INT32_T);
        }
        // 整数但位宽不符、而需要 i32 的兜底
        else if (have->is_integer_type() && need == INT32_T && have != INT32_T) {
            v = builder->create_zext(v, INT32_T);
        }

        args.push_back(v);
        ++param_ty_it;
    }
    return builder->create_call(callee, args);
}
