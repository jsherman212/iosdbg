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
	if(idx < 0)
		return 0;
	
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
	
	*str = realloc((*str), strlen((*str)) + strlen(saved) + 2);
	strcpy((*str) + idx, ")");
	strcat((*str), saved);
	free(saved);
}

int find_next_closing_paren(char *str, int start){
	size_t len = strlen(str);

	/* If we have '--' in front of an expression, not a single number,
	 * surrounded by parenthesis, we have to figure out where that
	 * expression ends in order to place the closing paren in the right place.
	 */
	if(strncmp(str + start, "(-(", 3) == 0){
		int idx = start + strlen("(-(") + 1;

		while(idx < len && str[idx] != ')'){
			/* We found an operator, so whatever is inside the
			 * parenthesis is an expression.
			 */
			if(is_operator(str[idx])){
				while(idx < len && str[idx] != ')')
					idx++;

				return idx;
			}

			idx++;
		}

		return -1;
	}
	else
		return -1;
}

/* Will insert a closing parenthesis if needed and return
 * the next index, otherwise will do nothing and 
 * return -1.
 */
int complete_expr(char **str, int idx){
	/* If there is more than one operand surrounded by parenthesis
	 * and one of those operands is negative, make sure we don't
	 * only wrap that operand in parenthesis rather than the
	 * entire expression in the parenthesis.
	 */
	int insert_idx = find_next_closing_paren((*str), idx);

	if(insert_idx == -1)
		insert_idx = get_next_operator_idx((*str), idx);

	if(insert_idx == -1){
		*str = realloc((*str), strlen((*str)) + 3);
		strcat((*str), ")");

		return -1;
	}
	
	insert_closing_paren_at_idx(str, insert_idx);
	
	return insert_idx;
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

/* Parse an expression.
 * `error` is set on error.
 */
long parse_expr(char *_expr, char **error){
	if(!_expr || (_expr && strlen(_expr) == 0)){
		asprintf(error, "empty expression string");
		return LONG_MIN;
	}

	char *expr = strdup(_expr);

	/* Strip whitespace from the beginning and the end of the string. */
	while(isblank(expr[0]))
		memmove(expr, expr + 1, strlen(expr));

	while(isblank(expr[strlen(expr) - 1]))
		expr[strlen(expr) - 1] = '\0';

	size_t exprlen = strlen(expr);

	/* Check for any "syntax errors" before we
	 * process this string.
	 */
	for(int i=1; i<exprlen; i++){
		if(invalid_expr(expr, expr[i], i)){
			asprintf(error, "unexpected operator '%c' at index %d\n"
					"error around here: %c%c%c%c\n"
					"%*s^", expr[i], i, exprlen > 0 && expr[i-1] != '\0' ? expr[i-1] : ' ', 
					exprlen > 1 && expr[i] != '\0' ? expr[i] : ' ', 
					exprlen > 2 && expr[i+1] != '\0' ? expr[i+1] : ' ', 
					exprlen > 3 && expr[i+2] != '\0' ? expr[i+2] : ' ', 
					(int)strlen("error around here") + 3, "");
			free(expr);
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
			
			/* Don't add extra parenthesis when we don't need to. */
			if(idx >= 1 && expr[idx-1] == '('){
				idx++;
				continue;
			}
			
			strcpy(expr + idx, "(");
			strcat(expr, saved);

			free(saved);

			idx = complete_expr(&expr, idx);

			if(idx == -1)
				break;
		}
		
		idx++;
	}

	struct stack_t *operators = stack_new();
	struct stack_t *operands = stack_new();

	/* Process and solve the expression. */
	idx = 0;
	size_t limit = strlen(expr);
	
	while(idx < limit){
		char current = expr[idx];

		/* Ignore any whitespace. */
		while(isblank(current)){
			idx++;
			continue;
		}

		if(isxdigit(current)){
			int base = isdigit(current) ? 10 : 16;

			/* Ignore any '0x'. */
			if(strncmp(expr + idx, "0x", strlen("0x")) == 0){
				idx += strlen("0x");
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

			long num = strtol(num_s, NULL, base);

			free(num_s);

			stack_push(operands, (void *)num);
		}
		else if(current == '('){
			stack_push(operators, (void *)'(');
			idx++;
		}
		else if(current == ')'){
			while(!stack_empty(operators) && (char)stack_peek(operators) != '('){
				char operator = (char)stack_pop(operators);

				long second = (long)stack_pop(operands);
				long first = (long)stack_pop(operands);

				char *err = NULL;

				long result = perform_operation(first, second, operator, &err);

				if(err){
					asprintf(error, "%s", err);

					free(expr);
					free(err);

					stack_free(operands);
					stack_free(operators);
										
					return LONG_MIN;
				}

				stack_push(operands, (void *)result);
			}

			stack_pop(operators);
			
			idx++;	
		}
		else if(is_operator(current)){
			if(is_neg_operator(expr, idx))
				stack_push(operands, (void *)0);

			char cur_op_precedence = precedence(current);

			while(!stack_empty(operators) && precedence((char)stack_peek(operators)) >= cur_op_precedence){
				char operator = (char)stack_pop(operators);

				long second = (long)stack_pop(operands);
				long first = (long)stack_pop(operands);

				char *err = NULL;

				long result = perform_operation(first, second, operator, &err);

				if(err){
					asprintf(error, "%s", err);

					free(err);
					free(expr);

					stack_free(operands);
					stack_free(operators);
					
					return LONG_MIN;
				}

				stack_push(operands, (void *)result);
			}

			stack_push(operators, (void *)(uintptr_t)(current));

			idx++;
		}
		else{
			asprintf(error, "bad character %c at index %d", current, idx);

			free(expr);

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
			free(expr);

			stack_free(operands);
			stack_free(operators);
			
			return LONG_MIN;
		}

		stack_push(operands, (void *)result);
	}

	long result = (long)stack_pop(operands);

	stack_free(operators);
	stack_free(operands);
	
	free(expr);

	return result;
}
