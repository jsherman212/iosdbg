#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "convvar.h"
#include "debuggee.h"
#include "stack.h"
#include "strext.h"

/* Use 'N' to express a unary negation. */
#define NEGATION 'N'

static long perform_operation(
        long left, long right, char operator, char **error){
    if(operator == '+')
        return left + right;
    else if(operator == '-')
        return left - right;
    else if(operator == '*')
        return left * right;
    else if(operator == '/'){
        if(right == 0){
            asprintf(error, "attempt to divide by zero");
            return LONG_MIN;
        }

        return left / right;
    }
    else{
        asprintf(error, "unknown operator '%c'", operator);
        return LONG_MIN;
    }
}

static int precedence(char operator){
    if(operator == '+' || operator == '-')
        return 1;
    else if(operator == '/' || operator == '*')
        return 2;
    else if(operator == NEGATION)
        return 3;
    else
        return 0;
}

/* Check if str[start, start+len] is a convenience variable. */
static int is_conv_var(char *str, int start, int len){
    char *target = substr(str, start, len);

    if(!target)
        return 0;

    if(strlen(target) == 0){
        free(target);
        return 0;
    }

    /* Convenience variables always start with $. */
    if(target[0] != '$'){
        free(target);
        return 0;
    }

    free(target);

    return 1;
}

static int is_operator(char c){
    return c == '+' ||
        c == '-' ||
        c == '*' ||
        c == '/';
}

static int is_neg_operator(char *str, int idx){
    if(idx < 0)
        return 0;

    if(idx == 0 && str[idx] == '-')
        return 1;

    if(str[idx] != '-')
        return 0;

    return str[idx] == '-' &&
        (str[idx - 1] == '(' || is_operator(str[idx - 1]));
}

/* Check if there are two operators in a row that
 * wouldn't make sense together.
 */
static int invalid_expr(char *str, char cur_op, int idx){
    if(idx == 0)
        return 0;

    if(idx == strlen(str))
        return 0;

    char prev_op = str[idx - 1];

    if(!(is_operator(prev_op) && is_operator(cur_op)))
        return 0;

    if(prev_op == cur_op){
        if(prev_op != '-')
            return 1;

        return 0;
    }

    return is_operator(prev_op) && (!isnumber(cur_op) &&
            !is_neg_operator(str, idx));
}

/* Check if we've reached the end of the number we're on
 * given a string and an index.
 */
static int is_end_of_number(char *str, int idx){
    if(!str)
        return 1;

    if(idx < 0 || idx > strlen(str))
        return 1;

    return (!is_neg_operator(str, idx) && is_operator(str[idx])) ||
        str[idx] == '(' ||
        str[idx] == ')';
}

/* Return the index of the end of the operand we
 * started on in `str`.
 */
static int find_end_of_operand(char *str, int start){
    if(!str)
        return -1;

    size_t slen = strlen(str);

    if(start < 0 || start > slen)
        return -1;

    int end = start;

    while(!is_end_of_number(str, end) && end < slen)
        end++;

    return end;
}

static char *lookup_register(char *reg, char **error){
    if(!reg)
        return NULL;

    /* Registers will always be two or three characters.
     * We also have to factor in the '$'.
     * We will ignore requests for CPSR, FPSR, and FPCR.
     */
    size_t reglen = strlen(reg);

    if(reglen < 3 || reglen > 4){
        asprintf(error, "bad register '%s'", reg);
        return NULL;
    }

    /* Put the string to lowercase so we don't
     * have to perform as many checks.
     */
    for(int i=0; i<reglen; i++)
        reg[i] = tolower(reg[i]);

    debuggee->get_thread_state();

    long long regval;

    if(strcmp(reg, "$fp") == 0)
        regval = debuggee->thread_state.__fp;
    else if(strcmp(reg, "$lr") == 0)
        regval = debuggee->thread_state.__lr;
    else if(strcmp(reg, "$sp") == 0)
        regval = debuggee->thread_state.__sp;
    else if(strcmp(reg, "$pc") == 0)
        regval = debuggee->thread_state.__pc;
    else{
        /* We cannot include floating point values. */
        if(reg[1] != 'x' && reg[1] != 'w'){
            asprintf(error, "bad register '%s'", reg);
            return NULL;
        }

        char *endptr = NULL;
        int regnum = strtol(reg + 2, &endptr, 10);

        if(endptr && *endptr){
            asprintf(error, "malformed register string '%s'", reg);
            return NULL;
        }

        if(reg[1] == 'x')
            regval = debuggee->thread_state.__x[regnum];
        else
            regval = debuggee->thread_state.__x[regnum] & 0xFFFFFFFF;
    }

    char *regvalstr;
    asprintf(&regvalstr, "%llx", regval);

    return regvalstr;
}

/* Replace any convenience variables in the expression
 * with their values.
 */
static void sub_conv_vars(char **expr, char **error){
    if(!expr || !(*expr))
        return;

    size_t exprlen = strlen(*expr);
    int idx = 0;

    while(idx < exprlen){
        if(!is_operator((*expr)[idx]) && !is_neg_operator(*expr, idx)){
            int end_of_operand = find_end_of_operand(*expr, idx);
            int len = end_of_operand - idx;
            
            if(is_conv_var(*expr, idx, len)){
                char *varstr = substr(*expr, idx, len);

                if(varstr){
                    /* Remove the convenience variable. */
                    strcut(expr, idx, end_of_operand - idx);

                    char *varval_s = convvar_strval(varstr, error);
                    
                    /* If this convenience variable doesn't exist, it
                     * may be a register.
                     */
                    if(error && *error){
                        *error = NULL;
                        varval_s = lookup_register(varstr, error);
                    }

                    /* Insert the value of that convenience variable. */
                    strins(expr, varval_s, idx);

                    free(varval_s);

                    exprlen = strlen(*expr);
                }
                else{
                    asprintf(error, "expected convenience variable");
                    return;
                }
            }
        }

        idx++;
    }
}

/* 6*2 and 6(2) mean the same thing. This function adds multiplication
 * operators in order to turn 6(2) and other expressions to 6*(2).
 */
static void add_mults(char **expr){
    if(!expr || !(*expr))
        return;

    size_t exprlen = strlen(*expr);
    int idx = 0;

    while(idx < exprlen){
        if(idx >= 1 && (*expr)[idx] == '(' &&
                !is_operator((*expr)[idx - 1]) && isnumber((*expr)[idx-1])){
            strins(expr, "*", idx);
            exprlen = strlen(*expr);
            idx++;
        }

        idx++;
    }
}

/* Do calculations with the operator and operand stack.
 * If we encounter an 'N', we have to negate what we just popped,
 * and continue the loop when we return.
 */
static long process_stacks(struct stack_t *operators, struct stack_t *operands,
        int *negative, char **error){
    char operator = (char)stack_pop(operators);

    long second = (long)stack_pop(operands);

    if(operator == NEGATION){
        second *= -1;
        stack_push(operands, (void *)second);
        *negative = 1;
        return 0;
    }

    long first = (long)stack_pop(operands);

    return perform_operation(first, second, operator, error);
}

/* Parse an expression.
 * `error` is set on error.
 */
static long evaluate(char *expr, char **error){
    size_t exprlen = strlen(expr);

    /* Check for any "syntax errors" before we
     * process this string.
     */
    for(int i=1; i<exprlen; i++){
        if(invalid_expr(expr, expr[i], i)){
            asprintf(error, "unexpected operator '%c' at index %d\n"
                    "error around here: %c%c%c%c\n"
                    "%*s^", expr[i], i,
                    exprlen > 0 && expr[i-1] != '\0' ? expr[i-1] : ' ', 
                    exprlen > 1 && expr[i] != '\0' ? expr[i] : ' ', 
                    exprlen > 2 && expr[i+1] != '\0' ? expr[i+1] : ' ', 
                    exprlen > 3 && expr[i+2] != '\0' ? expr[i+2] : ' ', 
                    (int)strlen("error around here") + 3, "");
            return LONG_MIN;
        }
    }
    
    struct stack_t *operators = stack_new();
    struct stack_t *operands = stack_new();

    const size_t zeroXlen = 2;

    /* Process and solve the expression. */
    int idx = 0;
    
    while(idx < exprlen){
        char current = expr[idx];

        /* Ignore any whitespace. */
        while(isblank(current)){
            idx++;
            current = expr[idx];
        }

        if(is_neg_operator(expr, idx))
            stack_push(operators, (void *)NEGATION);
        else if(isxdigit(current)){
            int base = isdigit(current) ? 10 : 16;

            /* Ignore any '0x'. */
            if(strncmp(expr + idx, "0x", zeroXlen) == 0){
                idx += zeroXlen;
                current = expr[idx];

                base = 16;
            }
            
            char *num_s = strdup(expr + idx);

            /* This number could be more than one digit. */
            while(isdigit(current) || isxdigit(current)){
                char c = tolower(current);

                if(c >= 'a' && c <= 'f')
                    base = 16;
                
                current = expr[++idx];
            }

            /* We had to advance one byte past the number to test
             * if we were past it, so decrement the index.
             */
            idx--;

            long num = strtol(num_s, NULL, base);

            free(num_s);

            stack_push(operands, (void *)num);
        }
        else if(current == '(')
            stack_push(operators, (void *)'(');
        else if(current == ')'){
            while(!stack_empty(operators) && (char)stack_peek(operators) != '('){
                int negative = 0;
                long result = process_stacks(operators, operands, &negative, error);

                if(negative)
                    continue;

                if(*error)
                    goto fail;

                stack_push(operands, (void *)result);
            }

            stack_pop(operators);
        }
        else if(is_operator(current)){
            char cur_op_precedence = precedence(current);

            while(!stack_empty(operators) &&
                    precedence((char)stack_peek(operators)) >= cur_op_precedence){
                int negative = 0;
                long result = process_stacks(operators, operands, &negative, error);

                if(negative)
                    continue;

                if(*error)
                    goto fail;

                stack_push(operands, (void *)result);
            }

            stack_push(operators, (void *)(uintptr_t)(current));
        }
        else{
            asprintf(error, "bad character %c at index %d", current, idx);
            goto fail;
        }

        idx++;
    }

    /* Perform the remaining operations to arrive at a final answer. */
    while(!stack_empty(operators)){
        int negative = 0;
        long result = process_stacks(operators, operands, &negative, error);

        if(negative)
            continue;

        if(*error)
            goto fail;

        stack_push(operands, (void *)result);
    }

    long result = (long)stack_pop(operands);

    stack_free(operators);
    stack_free(operands);

    return result;

fail:
    stack_free(operands);
    stack_free(operators);
    
    return LONG_MIN;
}

/* Parse an expression.
 * `error` is set on error.
 */
long parse_expr(char *_expr, char **error){
    if(!_expr || (_expr && strlen(_expr) == 0)){
        asprintf(error, "empty expression string");
        return LONG_MIN;
    }

    char *expr = strdup(_expr);

    /* 1. Get rid of unnecessary whitespace. */
    strclean(&expr);

    /* 2. Substitute any convenience variables. */
    sub_conv_vars(&expr, error);

    if(*error){
        free(expr);
        return LONG_MIN;
    }

    /* 3. Add any "missing" multiplication operators. */
    add_mults(&expr);

    /* 4. Evaluate the expression. */
    long result = evaluate(expr, error);

    if(*error){
        free(expr);
        return LONG_MIN;
    }

    free(expr);

    return result;
}
