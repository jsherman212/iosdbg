#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "convvar.h"
#include "linkedlist.h"
#include "printing.h"
#include "strext.h"

static struct linkedlist *vars;

/* A convenience variable cannot have the same name as a system register. */
static const char *const bad_names[] = {
    "$x0", "$x1", "$x2", "$x3", "$x4", "$x5", 
    "$x6", "$x7", "$x8", "$x9", "$x10", "$x11", "$x12", 
    "$x13", "$x14", "$x15", "$x16", "$x17", "$x18", 
    "$x19", "$x20", "$x21", "$x22", "$x23", "$x24", 
    "$x25", "$x26", "$x27", "$x28", "$fp", "$lr", "$sp",
    "$pc", "$xzr", "$w0", "$w1", "$w2", "$w3", "$w4", "$w5", 
    "$w6", "$w7", "$w8", "$w9", "$w10", "$w11", "$w12", 
    "$w13", "$w14", "$w15", "$w16", "$w17", "$w18", 
    "$w19", "$w20", "$w21", "$w22", "$w23", "$w24", 
    "$w25", "$w26", "$w27", "$w28", "$wsp", "$wzr",
    "$q0", "$q1", "$q2", "$q3", "$q4", "$q5", 
    "$q6", "$q7", "$q8", "$q9", "$q10", "$q11", "$q12", 
    "$q13", "$q14", "$q15", "$q16", "$q17", "$q18", 
    "$q19", "$q20", "$q21", "$q22", "$q23", "$q24", 
    "$q25", "$q26", "$q27", "$q28", "$q29", "$q30", "$q31", 
    "$v0", "$v1", "$v2", "$v3", "$v4", "$v5",
    "$v6", "$v7", "$v8", "$v9", "$v10", "$v11", "$v12",
    "$v13", "$v14", "$v15", "$v16", "$v17", "$v18",
    "$v19", "$v20", "$v21", "$v22", "$v23", "$v24",
    "$v25", "$v26", "$v27", "$v28", "$v29", "$v30", "$v31",
    "$d0", "$d1", "$d2", "$d3", "$d4", "$d5", 
    "$d6", "$d7", "$d8", "$d9", "$d10", "$d11", "$d12", 
    "$d13", "$d14", "$d15", "$d16", "$d17", "$d18", 
    "$d19", "$d20", "$d21", "$d22", "$d23", "$d24", 
    "$d25", "$d26", "$d27", "$d28", "$d29", "$d30", "$d31",
    "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", 
    "$s6", "$s7", "$s8", "$s9", "$s10", "$s11", "$s12", 
    "$s13", "$s14", "$s15", "$s16", "$s17", "$s18", 
    "$s19", "$s20", "$s21", "$s22", "$s23", "$s24", 
    "$s25", "$s26", "$s27", "$s28", "$s29", "$s30", "$s31",
    "$h0", "$h1", "$h2", "$h3", "$h4", "$h5",
    "$h6", "$h7", "$h8", "$h9", "$h10", "$h11", "$h12",
    "$h13", "$h14", "$h15", "$h16", "$h17", "$h18",
    "$h19", "$h20", "$h21", "$h22", "$h23", "$h24",
    "$h25", "$h26", "$h27", "$h28", "$h29", "$h30", "$h31",
    "$b0", "$b1", "$b2", "$b3", "$b4", "$b5", 
    "$b6", "$b7", "$b8", "$b9", "$b10", "$b11", "$b12", 
    "$b13", "$b14", "$b15", "$b16", "$b17", "$b18", 
    "$b19", "$b20", "$b21", "$b22", "$b23", "$b24", 
    "$b25", "$b26", "$b27", "$b28", "$b29", "$b30", "$b31",
    "$X0", "$X1", "$X2", "$X3", "$X4", "$X5", 
    "$X6", "$X7", "$X8", "$X9", "$X10", "$X11", "$X12", 
    "$X13", "$X14", "$X15", "$X16", "$X17", "$X18", 
    "$X19", "$X20", "$X21", "$X22", "$X23", "$X24", 
    "$X25", "$X26", "$X27", "$X28", "$FP", "$LR", "$SP",
    "$PC", "$XZR", "$W0", "$W1", "$W2", "$W3", "$W4", "$W5", 
    "$W6", "$W7", "$W8", "$W9", "$W10", "$W11", "$W12", 
    "$W13", "$W14", "$W15", "$W16", "$W17", "$W18", 
    "$W19", "$W20", "$W21", "$W22", "$W23", "$W24", 
    "$W25", "$W26", "$W27", "$W28", "$WSP", "$WZR",
    "$Q0", "$Q1", "$Q2", "$Q3", "$Q4", "$Q5", 
    "$Q6", "$Q7", "$Q8", "$Q9", "$Q10", "$Q11", "$Q12", 
    "$Q13", "$Q14", "$Q15", "$Q16", "$Q17", "$Q18", 
    "$Q19", "$Q20", "$Q21", "$Q22", "$Q23", "$Q24", 
    "$Q25", "$Q26", "$Q27", "$Q28", "$Q29", "$Q30", "$Q31", 
    "$V0", "$V1", "$V2", "$V3", "$V4", "$V5",
    "$V6", "$V7", "$V8", "$V9", "$V10", "$V11", "$V12",
    "$V13", "$V14", "$V15", "$V16", "$V17", "$V18",
    "$V19", "$V20", "$V21", "$V22", "$V23", "$V24",
    "$V25", "$V26", "$V27", "$V28", "$V29", "$V30", "$V31",
    "$D0", "$D1", "$D2", "$D3", "$D4", "$D5", 
    "$D6", "$D7", "$D8", "$D9", "$D10", "$D11", "$D12", 
    "$D13", "$D14", "$D15", "$D16", "$D17", "$D18", 
    "$D19", "$D20", "$D21", "$D22", "$D23", "$D24", 
    "$D25", "$D26", "$D27", "$D28", "$D29", "$D30", "$D31",
    "$S0", "$S1", "$S2", "$S3", "$S4", "$S5", 
    "$S6", "$S7", "$S8", "$S9", "$S10", "$S11", "$S12", 
    "$S13", "$S14", "$S15", "$S16", "$S17", "$S18", 
    "$S19", "$S20", "$S21", "$S22", "$S23", "$S24", 
    "$S25", "$S26", "$S27", "$S28", "$S29", "$S30", "$S31",
    "$H0", "$H1", "$H2", "$H3", "$H4", "$H5",
    "$H6", "$H7", "$H8", "$H9", "$H10", "$H11", "$H12",
    "$H13", "$H14", "$H15", "$H16", "$H17", "$H18",
    "$H19", "$H20", "$H21", "$H22", "$H23", "$H24",
    "$H25", "$H26", "$H27", "$H28", "$H29", "$H30", "$H31",
    "$B0", "$B1", "$B2", "$B3", "$B4", "$B5", 
    "$B6", "$B7", "$B8", "$B9", "$B10", "$B11", "$B12", 
    "$B13", "$B14", "$B15", "$B16", "$B17", "$B18", 
    "$B19", "$B20", "$B21", "$B22", "$B23", "$B24", 
    "$B25", "$B26", "$B27", "$B28", "$B29", "$B30", "$B31",
    "$fp", "$lr", "$sp", "$pc", 
    "$FP", "$LR", "$SP", "$PC", 
    "$cpsr", "$fpsr", "$fpcr",
    "$CPSR", "$FPSR", "$FPCR"
};

static int invalid_name(char *name){
    if(!name)
        return 1;

    int len = sizeof(bad_names) / sizeof(const char *);

    for(int i=0; i<len; i++){
        if(strcmp(name, bad_names[i]) == 0)
            return 1;
    }

    return 0;
}

static int convvar_exists(char *name, char **error){
    if(!vars)
        return 0;

    if(name && name[0] != '$'){
        concat(error, "names of convenience variables must start with '$'");
        return 0;
    }

    return lookup_convvar(name) != NULL;
}

static enum convvar_kind determine_kind(char *value, char **error){
    if(value && strlen(value) == 0)
        return CONVVAR_VOID_KIND;

    char *first_decimal = strchr(value, '.');
    char *last_decimal = strrchr(value, '.');

    /* If the user wants to store a string, there will be
     * value will contain quotations.
     */
    if(value[0] == '"'){
        if(!strrchr(value, '"')){
            concat(error, "missing closing quotation for string '%s'", value);
            return -1;
        }

        return CONVVAR_STRING;
    }
    else if(first_decimal != last_decimal){
        concat(error, "malformed floating point number '%s'", value);
        return -1;
    }
    else if(first_decimal)
        return CONVVAR_DOUBLE;
    else
        return CONVVAR_INTEGER;
}

static void update_convvar(struct convvar *var, char *value, char **error){
    if(!value){
        concat(error, "NULL value");
        return;
    }

    if(strlen(value) == 0){
        var->state = CONVVAR_VOID;
        return;
    }

    char *endptr = NULL;
    
    if(var->kind == CONVVAR_INTEGER)
        var->data.integer = strtoll(value, &endptr, 0);
    else if(var->kind == CONVVAR_DOUBLE){
        double d = strtod(value, &endptr);
        var->data.integer = *(long long *)&d;
    }
    else if(var->kind == CONVVAR_STRING){
        /* Don't include the quotation marks. */
        size_t valuelen = strlen(value);
        memmove(value, value + 1, valuelen);
        value[valuelen - 2] = '\0';

        var->data.string = strdup(value);
    }   

    if(endptr && *endptr != '\0'){
        concat(error, "invalid number '%s'", value);
        convvar_free(var);
        
        return;
    }

    var->state = CONVVAR_NONVOID;
}

static void create_convvar(char *name, char *value, char **error){
    enum convvar_kind kind = determine_kind(value, error);

    if(*error)
        return;

    struct convvar *var = malloc(sizeof(struct convvar));
    var->name = strdup(name);

    if(kind == CONVVAR_VOID_KIND)
        var->state = CONVVAR_VOID;
    else
        var->state = CONVVAR_NONVOID;

    var->kind = kind;

    update_convvar(var, value, error);
    
    if(*error)
        return;

    if(!vars)
        vars = linkedlist_new();

    linkedlist_add(vars, var);
}

struct convvar *lookup_convvar(char *name){
    if(!vars)
        return NULL;

    if(invalid_name(name))
        return NULL;

    struct node_t *current = vars->front;
    
    while(current){
        struct convvar *var = current->data;

        if(strcmp(var->name, name) == 0)
            return var;

        current = current->next;
    }

    return NULL;
}

char *convvar_strval(char *name, char **error){
    struct convvar *var = lookup_convvar(name);

    if(!var){
        concat(error, "convenience variable '%s' does not exist", name);
        return NULL;
    }

    if(var->state == CONVVAR_VOID)
        return strdup("void");

    if(var->kind == CONVVAR_STRING)
        return strdup(var->data.string);

    char *s = NULL;

    if(var->kind == CONVVAR_DOUBLE)
        concat(&s, "%f", *(double *)&var->data.integer);
    else{
        long long i = var->data.integer;
        concat(&s, "%s%#llx", i < 0 ? "-" : "", i < 0 ? -i : i);
    }

    return s;
}

void void_convvar(char *name){
    if(!name)
        return;
    
    name = strdup(name);

    struct convvar *target = lookup_convvar(name);

    free(name);

    if(!target)
        return;

    target->state = CONVVAR_VOID;
}

/* `name` and `value` must be malloc'ed */
void set_convvar(char *name, char *value, char **error){
    if(invalid_name(name)){
        concat(error, "'%s' is an invalid convenience variable name",
                name ? name : "NULL");
        return;
    }
    
    if(!convvar_exists(name, error)){
        if(*error)
            return;
        
        create_convvar(name, value, error);
    }
    else{
        struct convvar *target = lookup_convvar(name);

        if(target){
            target->kind = determine_kind(value, error);
            update_convvar(target, value, error);
        }
    }
}

void del_convvar(char *name, char **error){
    struct convvar *target = lookup_convvar(name);

    if(!target){
        concat(error, "convenience variable '%s' not found", name);
        return;
    }

    convvar_free(target);

    if(vars)
        linkedlist_delete(vars, target);
}

void p_convvar(char *name, char **outbuffer){
    if(!name || !vars)
        return;

    char *error = NULL;
    char *sval = convvar_strval(name, &error);

    concat(outbuffer, "%8s", "");

    if(error)
        concat(outbuffer, "failed to show convenience variable '%s': %s\n", name, error);
    else
        concat(outbuffer, "%s = %s\n", name, sval);

    free(sval);
}

void show_all_cvars(char **outbuffer){
    if(!vars)
        return;

    struct node_t *current = vars->front;
    
    while(current){
        struct convvar *var = current->data;

        p_convvar(var->name, outbuffer);

        current = current->next;
    }
}

void desc_auto_convvar_error_if_needed(char **outbuffer, char *var, char *e){
    if(!e || !var)
        return;

    concat(outbuffer, "could not automatically update the"
            "convenience variable '%s': %s\n",
            var, e);
}

void convvar_free(struct convvar *var){
    if(!var)
        return;

    if(var->name)
        free(var->name);

    free(var);
}
