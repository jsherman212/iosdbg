#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "branch.h"

static enum bikind figure_kind(unsigned int opcode){
    unsigned int op0 = opcode >> 29;
    unsigned int op1 = (opcode & 0x3fff000) >> 25;

    if((op0 & ~4) == 0)
        return UNCOND_BRANCH_IMMEDIATE;

    if(op1 == 1){
        if(op0 == 6)
            return UNCOND_BRANCH_REGISTER;
        if((op0 & ~4) == 1)
            return TEST_AND_BRANCH_IMMEDIATE;
    }
    else{
        if(op0 == 2)
            return COND_BRANCH_IMMEDIATE;
        if((op0 & ~4) == 1)
            return COMP_AND_BRANCH_IMMEDIATE;
    }

    return UNKNOWN_KIND;
}

static const char *cond_table[] = {
    "eq,ne", "cs,cc", "mi,pl", "vs,vc",
    "hi,ls", "ge,lt", "gt,le", "al"
};

enum bicond figure_cond(unsigned int opcode){
    unsigned int cond = opcode & ~0x7ffffff0;
    unsigned int shifted = cond >> 1;

    char decoded[8] = {0};

    if((cond & 1) == 1 && cond != 0xf)
        sprintf(decoded, "%s", cond_table[shifted] + 3);
    else
        snprintf(decoded, 3, "%s", cond_table[shifted]);

    // XXX find a pattern later
    if(strcmp(decoded, "eq") == 0)
        return EQ;
    else if(strcmp(decoded, "ne") == 0)
        return NE;
    else if(strcmp(decoded, "cs") == 0)
        return CS;
    else if(strcmp(decoded, "cc") == 0)
        return CC;
    else if(strcmp(decoded, "mi") == 0)
        return MI;
    else if(strcmp(decoded, "pl") == 0)
        return PL;
    else if(strcmp(decoded, "vs") == 0)
        return VS;
    else if(strcmp(decoded, "vc") == 0)
        return VC;
    else if(strcmp(decoded, "hi") == 0)
        return HI;
    else if(strcmp(decoded, "ls") == 0)
        return LS;
    else if(strcmp(decoded, "ge") == 0)
        return GE;
    else if(strcmp(decoded, "lt") == 0)
        return LT;
    else if(strcmp(decoded, "gt") == 0)
        return GT;
    else if(strcmp(decoded, "le") == 0)
        return LE;
    else if(strcmp(decoded, "al") == 0)
        return AL;
    else
        return UNKNOWN_COND;
}

/* If this opcode is a branch, binfo is filled. Otherwise its contents are undefined */
int is_branch(unsigned int opcode, struct branchinfo *binfo){
    unsigned int op0 = (opcode << 3) >> 29;

    /* Not a branch */
    if(op0 != 5)
        return 0;

    binfo->kind = figure_kind(opcode);
    binfo->is_subroutine_call = 0;
    binfo->conditional = 0;
    binfo->cond = UNKNOWN_COND;

    if(binfo->kind == COND_BRANCH_IMMEDIATE){
        binfo->conditional = 1;
        binfo->cond = figure_cond(opcode);
    }

    binfo->imm = INT_MAX;

    /* Both have 19 bit immediates */
    if(binfo->kind == COND_BRANCH_IMMEDIATE ||
            binfo->kind == COMP_AND_BRANCH_IMMEDIATE){
        binfo->imm = ((opcode & 0xffffe0) >> 5) * 4;
    }
    else if(binfo->kind == UNCOND_BRANCH_IMMEDIATE){
        binfo->imm = (opcode & 0x3ffffff) * 4;
        binfo->is_subroutine_call = opcode >> 31;
    }
    else if(binfo->kind == TEST_AND_BRANCH_IMMEDIATE){
        binfo->imm = ((opcode & 0x3fff) >> 5) * 4;
    }

    if(binfo->kind == UNCOND_BRANCH_REGISTER){
        unsigned int opc = (opcode >> 21) & 0xf;

        if(opc == 1 || opc == 9)
            binfo->is_subroutine_call = 1;
    }

    binfo->rn = NONE;

    if(binfo->kind == UNCOND_BRANCH_REGISTER)
        binfo->rn = (opcode & 0x3e0) >> 5;
    else if(binfo->kind == COMP_AND_BRANCH_IMMEDIATE ||
            binfo->kind == TEST_AND_BRANCH_IMMEDIATE){
        binfo->rn = opcode & 0x1f;

        /* 32 bit */
        if(!(opcode >> 31))
            binfo->rn += 32;
    }

    return 1;
}
