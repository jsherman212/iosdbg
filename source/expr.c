#include "expr.h"

long perform_operation(long left, long right, char operator, char **error){
	if(operator == '+')
		return left + right;
	else if(operator == '-')
		return left - right;
	else if(operator == '*')
		return left * right;
	else if(operator == '/')
		return left / right;
	else{
		asprintf(error, "Unknown operator '%c'", operator);
		return LONG_MIN;
	}
}

int precedence(char operator){
	if(operator == '+' || operator == '-')
		return 1;
	else if(operator == '/' || operator == '*')
		return 2;
	else
		return 0;
}

int is_operator(char c){
	return c == '+' ||
		c == '-' ||
		c == '*' ||
		c == '/';
}

int is_neg_operator(char *str, int idx){
	if(idx == 0 && str[idx] == '-')
		return 1;
	
	if(str[idx] != '-')
		return 0;

	return str[idx] == '-' &&
		(str[idx - 1] == '(' || is_operator(str[idx - 1]));
}

int get_next_operator_idx(char *str, int start){
	size_t len = strlen(str);

	for(int i=start; i<len; i++){
		if(!is_neg_operator(str, i) && is_operator(str[i]))
			return i;
	}

	return -1;
}

void insert_closing_paren_at_idx(char **str, int idx){
	char *saved = strdup((*str) + idx);
	
	(*str) = realloc((*str), strlen((*str)) + strlen(saved) + 2);
	strcpy((*str) + idx, ")");
	strcat((*str), saved);
	free(saved);
}

/* Will insert a closing parenthesis if needed and return
 * the next index, otherwise will do nothing and 
 * return -1.
 */
int complete_expr(char **str, int idx){
	int next_op_idx = get_next_operator_idx((*str), idx);

	if(next_op_idx == -1){
		*str = realloc((*str), strlen((*str)) + 3);
		strcat((*str), ")");
		return -1;
	}
	
	insert_closing_paren_at_idx(str, next_op_idx);
	
	return next_op_idx;
}

/* Check if there are two operators in a row that
 * wouldn't make sense together.
 */
int invalid_expr(char *str, char cur_op, int idx){
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

		return is_neg_operator(str, idx - 1) && is_neg_operator(str, idx);
	}
	
	return is_operator(prev_op) && (!isnumber(cur_op) && !is_neg_operator(str, idx));
}

/* Solve an expression. expr must point to a block
 * of memory allocated by malloc.
 */
long parse_expr(char *expr, char **error){
	if(!expr){
		asprintf(error, "NULL expression string");
		return LONG_MIN;
	}

	struct stack_t *operators = stack_new();
	struct stack_t *operands = stack_new();

	size_t exprlen = strlen(expr);

	/* Check for any "syntax errors" before we
	 * process this string.
	 */
	for(int i=1; i<exprlen; i++){
		if(invalid_expr(expr, expr[i], i)){
			free(expr);
			asprintf(error, "unexpected operator '%c' at index %d", expr[i], i);
			return LONG_MIN;
		}
	}

	/* Put parenthesis around all negative numbers. */
	int idx = 0;

	while(idx < strlen(expr)){
		char current = expr[idx];

		if(idx == 0 && current == '-'){
			char *temp = malloc(strlen(expr) + 2);
			strcpy(temp, "(");
			strcat(temp, expr);

			expr = realloc(expr, strlen(temp) + 1);
			strcpy(expr, temp);
	
			idx = complete_expr(&expr, idx);

			if(idx == -1)
				break;
		}
		else if(is_neg_operator(expr, idx)){
			char *saved = strdup(expr + idx);
			expr = realloc(expr, strlen(expr) + strlen(saved) + 2);
			strcpy(expr + idx, "(");
			strcat(expr, saved);

			free(saved);

			idx = complete_expr(&expr, idx);

			if(idx == -1)
				break;
		}
		
		idx++;
	}
	
	/* Process and solve the expression. */
	while(*expr){
		/* Ignore any whitespace. */
		while(isblank(*expr))
			expr++;

		if(isxdigit(*expr)){
			int base = isdigit(*expr) ? 10 : 16;

			/* Strip off any '0x'. */
			if(strncmp(expr, "0x", strlen("0x")) == 0){
				expr += strlen("0x");
				base = 16;
			}
			
			char *num_s = strdup(expr);
			char *start = expr;

			/* This number could be more than one digit. */
			while(isdigit(*expr) || isxdigit(*expr)){
				char c = tolower(*expr);

				if(c >= 'a' && c <= 'f')
					base = 16;
				
				expr++;
			}

			num_s[expr - start] = '\0';

			long num = strtol(num_s, NULL, base);

			free(num_s);

			stack_push(operands, (void *)num);
		}
		else if(*expr == '('){
			stack_push(operators, (void *)'(');
			expr++;
		}
		else if(*expr == ')'){
			while(!stack_empty(operators) && (char)stack_peek(operators) != '('){
				char operator = (char)stack_pop(operators);

				long second = (long)stack_pop(operands);
				long first = (long)stack_pop(operands);

				char *err = NULL;

				long result = perform_operation(first, second, operator, &err);

				if(err){
					asprintf(error, "%s", err);

					free(err);
					stack_free(operands);
					stack_free(operators);
					
					return LONG_MIN;
				}

				stack_push(operands, (void *)result);
			}

			stack_pop(operators);
			
			expr++;
		}
		else if(is_operator(*expr)){
			/* Take care of negative numbers. */
			if(*expr == '-'){
				if(*(expr - 1) == '(' || is_operator(*(expr - 1)))
					stack_push(operands, (void *)0);
			}

			char cur_op_precedence = precedence(*expr);

			while(!stack_empty(operators) && precedence((char)stack_peek(operators)) >= cur_op_precedence){
				char operator = (char)stack_pop(operators);

				long second = (long)stack_pop(operands);
				long first = (long)stack_pop(operands);

				char *err = NULL;

				long result = perform_operation(first, second, operator, &err);

				if(err){
					asprintf(error, "%s", err);

					free(err);
					stack_free(operands);
					stack_free(operators);
					
					return LONG_MIN;
				}

				stack_push(operands, (void *)result);
			}

			stack_push(operators, (void *)(uintptr_t)(*expr));

			expr++;
		}
		else{
			asprintf(error, "bad character %c at index %d", *expr, idx);

			stack_free(operands);
			stack_free(operators);
			
			return LONG_MIN;
		}
	}

	/* Perform the remaining operations to arrive at a final answer. */
	while(!stack_empty(operators)){
		char operator = (char)stack_pop(operators);

		long second = (long)stack_pop(operands);
		long first = (long)stack_pop(operands);

		char *err = NULL;

		long result = perform_operation(first, second, operator, &err);

		if(err){
			asprintf(error, "%s", err);

			free(err);
			stack_free(operands);
			stack_free(operators);
			
			return LONG_MIN;
		}

		stack_push(operands, (void *)result);
	}

	long result = (long)stack_pop(operands);

	stack_free(operators);
	stack_free(operands);

	return result;
}
