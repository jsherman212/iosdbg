#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dwarf.h>

#include "../expr.h"
#include "../memutils.h"
#include "../strext.h"
#include "../thread.h"

#include "common.h"

struct dwarf_locdesc {
    uint64_t locdesc_lopc;
    uint64_t locdesc_hipc;
    int locdesc_bounded;
    Dwarf_Small locdesc_op;
    Dwarf_Unsigned locdesc_opd1;
    Dwarf_Unsigned locdesc_opd2;
    Dwarf_Unsigned locdesc_opd3;
    Dwarf_Unsigned locdesc_offsetforbranch;

    struct dwarf_locdesc *locdesc_prev;
    struct dwarf_locdesc *locdesc_next;
};

int is_locdesc_in_bounds(struct dwarf_locdesc *locdesc,
        uint64_t pc){
    if(!locdesc->locdesc_bounded)
        return 1;

    return pc >= locdesc->locdesc_lopc && pc < locdesc->locdesc_hipc;
}

/* #define\s+(DW_OP_\w+)\s*0x[[:xdigit:]]+ */
static const char *get_op_name(Dwarf_Small op){
    switch(op){
        case DW_OP_addr:
            return "DW_OP_addr";
        case DW_OP_deref:
            return "DW_OP_deref";
        case DW_OP_const1u:
            return "DW_OP_const1u";
        case DW_OP_const1s:
            return "DW_OP_const1s";
        case DW_OP_const2u:
            return "DW_OP_const2u";
        case DW_OP_const2s:
            return "DW_OP_const2s";
        case DW_OP_const4u:
            return "DW_OP_const4u";
        case DW_OP_const4s:
            return "DW_OP_const4s";
        case DW_OP_const8u:
            return "DW_OP_const8u";
        case DW_OP_const8s:
            return "DW_OP_const8s";
        case DW_OP_constu:
            return "DW_OP_constu";
        case DW_OP_consts:
            return "DW_OP_consts";
        case DW_OP_dup:
            return "DW_OP_dup";
        case DW_OP_drop:
            return "DW_OP_drop";
        case DW_OP_over:
            return "DW_OP_over";
        case DW_OP_pick:
            return "DW_OP_pick";
        case DW_OP_swap:
            return "DW_OP_swap";
        case DW_OP_rot:
            return "DW_OP_rot";
        case DW_OP_xderef:
            return "DW_OP_xderef";
        case DW_OP_abs:
            return "DW_OP_abs";
        case DW_OP_and:
            return "DW_OP_and";
        case DW_OP_div:
            return "DW_OP_div";
        case DW_OP_minus:
            return "DW_OP_minus";
        case DW_OP_mod:
            return "DW_OP_mod";
        case DW_OP_mul:
            return "DW_OP_mul";
        case DW_OP_neg:
            return "DW_OP_neg";
        case DW_OP_not:
            return "DW_OP_not";
        case DW_OP_or:
            return "DW_OP_or";
        case DW_OP_plus:
            return "DW_OP_plus";
        case DW_OP_plus_uconst:
            return "DW_OP_plus_uconst";
        case DW_OP_shl:
            return "DW_OP_shl";
        case DW_OP_shr:
            return "DW_OP_shr";
        case DW_OP_shra:
            return "DW_OP_shra";
        case DW_OP_xor:
            return "DW_OP_xor";
        case DW_OP_bra:
            return "DW_OP_bra";
        case DW_OP_eq:
            return "DW_OP_eq";
        case DW_OP_ge:
            return "DW_OP_ge";
        case DW_OP_gt:
            return "DW_OP_gt";
        case DW_OP_le:
            return "DW_OP_le";
        case DW_OP_lt:
            return "DW_OP_lt";
        case DW_OP_ne:
            return "DW_OP_ne";
        case DW_OP_skip:
            return "DW_OP_skip";
        case DW_OP_lit0:
            return "DW_OP_lit0";
        case DW_OP_lit1:
            return "DW_OP_lit1";
        case DW_OP_lit2:
            return "DW_OP_lit2";
        case DW_OP_lit3:
            return "DW_OP_lit3";
        case DW_OP_lit4:
            return "DW_OP_lit4";
        case DW_OP_lit5:
            return "DW_OP_lit5";
        case DW_OP_lit6:
            return "DW_OP_lit6";
        case DW_OP_lit7:
            return "DW_OP_lit7";
        case DW_OP_lit8:
            return "DW_OP_lit8";
        case DW_OP_lit9:
            return "DW_OP_lit9";
        case DW_OP_lit10:
            return "DW_OP_lit10";
        case DW_OP_lit11:
            return "DW_OP_lit11";
        case DW_OP_lit12:
            return "DW_OP_lit12";
        case DW_OP_lit13:
            return "DW_OP_lit13";
        case DW_OP_lit14:
            return "DW_OP_lit14";
        case DW_OP_lit15:
            return "DW_OP_lit15";
        case DW_OP_lit16:
            return "DW_OP_lit16";
        case DW_OP_lit17:
            return "DW_OP_lit17";
        case DW_OP_lit18:
            return "DW_OP_lit18";
        case DW_OP_lit19:
            return "DW_OP_lit19";
        case DW_OP_lit20:
            return "DW_OP_lit20";
        case DW_OP_lit21:
            return "DW_OP_lit21";
        case DW_OP_lit22:
            return "DW_OP_lit22";
        case DW_OP_lit23:
            return "DW_OP_lit23";
        case DW_OP_lit24:
            return "DW_OP_lit24";
        case DW_OP_lit25:
            return "DW_OP_lit25";
        case DW_OP_lit26:
            return "DW_OP_lit26";
        case DW_OP_lit27:
            return "DW_OP_lit27";
        case DW_OP_lit28:
            return "DW_OP_lit28";
        case DW_OP_lit29:
            return "DW_OP_lit29";
        case DW_OP_lit30:
            return "DW_OP_lit30";
        case DW_OP_lit31:
            return "DW_OP_lit31";
        case DW_OP_reg0:
            return "DW_OP_reg0";
        case DW_OP_reg1:
            return "DW_OP_reg1";
        case DW_OP_reg2:
            return "DW_OP_reg2";
        case DW_OP_reg3:
            return "DW_OP_reg3";
        case DW_OP_reg4:
            return "DW_OP_reg4";
        case DW_OP_reg5:
            return "DW_OP_reg5";
        case DW_OP_reg6:
            return "DW_OP_reg6";
        case DW_OP_reg7:
            return "DW_OP_reg7";
        case DW_OP_reg8:
            return "DW_OP_reg8";
        case DW_OP_reg9:
            return "DW_OP_reg9";
        case DW_OP_reg10:
            return "DW_OP_reg10";
        case DW_OP_reg11:
            return "DW_OP_reg11";
        case DW_OP_reg12:
            return "DW_OP_reg12";
        case DW_OP_reg13:
            return "DW_OP_reg13";
        case DW_OP_reg14:
            return "DW_OP_reg14";
        case DW_OP_reg15:
            return "DW_OP_reg15";
        case DW_OP_reg16:
            return "DW_OP_reg16";
        case DW_OP_reg17:
            return "DW_OP_reg17";
        case DW_OP_reg18:
            return "DW_OP_reg18";
        case DW_OP_reg19:
            return "DW_OP_reg19";
        case DW_OP_reg20:
            return "DW_OP_reg20";
        case DW_OP_reg21:
            return "DW_OP_reg21";
        case DW_OP_reg22:
            return "DW_OP_reg22";
        case DW_OP_reg23:
            return "DW_OP_reg23";
        case DW_OP_reg24:
            return "DW_OP_reg24";
        case DW_OP_reg25:
            return "DW_OP_reg25";
        case DW_OP_reg26:
            return "DW_OP_reg26";
        case DW_OP_reg27:
            return "DW_OP_reg27";
        case DW_OP_reg28:
            return "DW_OP_reg28";
        case DW_OP_reg29:
            return "DW_OP_reg29";
        case DW_OP_reg30:
            return "DW_OP_reg30";
        case DW_OP_reg31:
            return "DW_OP_reg31";
        case DW_OP_breg0:
            return "DW_OP_breg0";
        case DW_OP_breg1:
            return "DW_OP_breg1";
        case DW_OP_breg2:
            return "DW_OP_breg2";
        case DW_OP_breg3:
            return "DW_OP_breg3";
        case DW_OP_breg4:
            return "DW_OP_breg4";
        case DW_OP_breg5:
            return "DW_OP_breg5";
        case DW_OP_breg6:
            return "DW_OP_breg6";
        case DW_OP_breg7:
            return "DW_OP_breg7";
        case DW_OP_breg8:
            return "DW_OP_breg8";
        case DW_OP_breg9:
            return "DW_OP_breg9";
        case DW_OP_breg10:
            return "DW_OP_breg10";
        case DW_OP_breg11:
            return "DW_OP_breg11";
        case DW_OP_breg12:
            return "DW_OP_breg12";
        case DW_OP_breg13:
            return "DW_OP_breg13";
        case DW_OP_breg14:
            return "DW_OP_breg14";
        case DW_OP_breg15:
            return "DW_OP_breg15";
        case DW_OP_breg16:
            return "DW_OP_breg16";
        case DW_OP_breg17:
            return "DW_OP_breg17";
        case DW_OP_breg18:
            return "DW_OP_breg18";
        case DW_OP_breg19:
            return "DW_OP_breg19";
        case DW_OP_breg20:
            return "DW_OP_breg20";
        case DW_OP_breg21:
            return "DW_OP_breg21";
        case DW_OP_breg22:
            return "DW_OP_breg22";
        case DW_OP_breg23:
            return "DW_OP_breg23";
        case DW_OP_breg24:
            return "DW_OP_breg24";
        case DW_OP_breg25:
            return "DW_OP_breg25";
        case DW_OP_breg26:
            return "DW_OP_breg26";
        case DW_OP_breg27:
            return "DW_OP_breg27";
        case DW_OP_breg28:
            return "DW_OP_breg28";
        case DW_OP_breg29:
            return "DW_OP_breg29";
        case DW_OP_breg30:
            return "DW_OP_breg30";
        case DW_OP_breg31:
            return "DW_OP_breg31";
        case DW_OP_regx:
            return "DW_OP_regx";
        case DW_OP_fbreg:
            return "DW_OP_fbreg";
        case DW_OP_bregx:
            return "DW_OP_bregx";
        case DW_OP_piece:
            return "DW_OP_piece";
        case DW_OP_deref_size:
            return "DW_OP_deref_size";
        case DW_OP_xderef_size:
            return "DW_OP_xderef_size";
        case DW_OP_nop:
            return "DW_OP_nop";
        case DW_OP_push_object_address:
            return "DW_OP_push_object_address";
        case DW_OP_call2:
            return "DW_OP_call2";
        case DW_OP_call4:
            return "DW_OP_call4";
        case DW_OP_call_ref:
            return "DW_OP_call_ref";
        case DW_OP_form_tls_address:
            return "DW_OP_form_tls_address";
        case DW_OP_call_frame_cfa:
            return "DW_OP_call_frame_cfa";
        case DW_OP_bit_piece:
            return "DW_OP_bit_piece";
        case DW_OP_implicit_value:
            return "DW_OP_implicit_value";
        case DW_OP_stack_value:
            return "DW_OP_stack_value";
        case DW_OP_implicit_pointer:
            return "DW_OP_implicit_pointer";
        case DW_OP_addrx:
            return "DW_OP_addrx";
        case DW_OP_constx:
            return "DW_OP_constx";
        case DW_OP_entry_value:
            return "DW_OP_entry_value";
        case DW_OP_const_type:
            return "DW_OP_const_type";
        case DW_OP_regval_type:
            return "DW_OP_regval_type";
        case DW_OP_deref_type:
            return "DW_OP_deref_type";
        case DW_OP_xderef_type:
            return "DW_OP_xderef_type";
        case DW_OP_convert:
            return "DW_OP_convert";
        case DW_OP_reinterpret:
            return "DW_OP_reinterpret";
        default:
            return "<unknown expression opcode>";
    };
}

static char *get_register_name(Dwarf_Half op){
    char regstr[8] = {0};

    if(op < 29)
        snprintf(regstr, sizeof(regstr), "$x%d", op);
    else if(op == 29)
        strcat(regstr, "$fp");
    else if(op == 30)
        strcat(regstr, "$lr");
    else if(op == 31)
        strcat(regstr, "$sp");

    return strdup(regstr);
}

static uint64_t get_register_value(int rn){
    if(rn < 0 || rn > 31)
        return 0;

    struct machthread *focused = get_focused_thread();

    if(rn < 29)
        return focused->thread_state.__x[rn];
    else if(rn == 29)
        return focused->thread_state.__fp;
    else if(rn == 30)
        return focused->thread_state.__lr;
    else
        return focused->thread_state.__sp;
}

void add_additional_location_description(Dwarf_Half whichattr,
        struct dwarf_locdesc **locs, struct dwarf_locdesc *add,
        int idx){
    struct dwarf_locdesc *current = locs[idx];

    while(current->locdesc_next)
        current = current->locdesc_next;

    current->locdesc_next = add;
    add->locdesc_prev = current;
}

static struct dwarf_locdesc *create_new_locdesc(int bounded,
        uint64_t locdesc_lopc, uint64_t locdesc_hipc, Dwarf_Small op,
        Dwarf_Unsigned opd1, Dwarf_Unsigned opd2, Dwarf_Unsigned opd3,
        Dwarf_Unsigned offsetforbranch){
    struct dwarf_locdesc *locdesc = calloc(1, sizeof(struct dwarf_locdesc));

    if(bounded){
        locdesc->locdesc_bounded = 1;

        locdesc->locdesc_lopc = locdesc_lopc;
        locdesc->locdesc_hipc = locdesc_hipc;
    }

    locdesc->locdesc_op = op;
    locdesc->locdesc_opd1 = opd1;
    locdesc->locdesc_opd2 = opd2;
    locdesc->locdesc_opd3 = opd3;
    locdesc->locdesc_offsetforbranch = offsetforbranch;

    return locdesc;
}

struct dwarf_locdesc *copy_locdesc(struct dwarf_locdesc *based_on){
    if(!based_on)
        return NULL;

    struct dwarf_locdesc *root = create_new_locdesc(based_on->locdesc_bounded,
            based_on->locdesc_lopc, based_on->locdesc_hipc,
            based_on->locdesc_op, based_on->locdesc_opd1,
            based_on->locdesc_opd2, based_on->locdesc_opd3,
            based_on->locdesc_offsetforbranch);

    struct dwarf_locdesc *paramcurrent = based_on->locdesc_next;
    struct dwarf_locdesc *rootcurrent = root;

    while(paramcurrent){
        struct dwarf_locdesc *copied =
            create_new_locdesc(paramcurrent->locdesc_bounded,
                paramcurrent->locdesc_lopc, paramcurrent->locdesc_hipc,
                paramcurrent->locdesc_op, paramcurrent->locdesc_opd1,
                paramcurrent->locdesc_opd2, paramcurrent->locdesc_opd3,
                paramcurrent->locdesc_offsetforbranch);
        
        rootcurrent->locdesc_next = copied;
        copied->locdesc_prev = rootcurrent;

        rootcurrent = rootcurrent->locdesc_next;
        paramcurrent = paramcurrent->locdesc_next;
    }

    return root;
}

void *create_location_description(Dwarf_Small loclist_source,
        uint64_t locdesc_lopc, uint64_t locdesc_hipc,
        Dwarf_Small op, Dwarf_Unsigned opd1,
        Dwarf_Unsigned opd2, Dwarf_Unsigned opd3,
        Dwarf_Unsigned offsetforbranch){
    int bounded = 0;

    if(loclist_source == LOCATION_LIST_ENTRY)
        bounded = 1;
    
    return create_new_locdesc(bounded, locdesc_lopc, locdesc_hipc,
            op, opd1, opd2, opd3, offsetforbranch);
}

/* Returns the register DW_AT_frame_base represents */
static char *evaluate_frame_base(struct dwarf_locdesc *framebaselocdesc){
    Dwarf_Small op = framebaselocdesc->locdesc_op;

    switch(op){
        case DW_OP_reg0...DW_OP_reg31:
            return get_register_name(op - DW_OP_reg0);
        case DW_OP_regx:
            return get_register_name(framebaselocdesc->locdesc_opd1);
        default:
            return NULL;
    };
}

// XXX with the help of my expression evaluator, this will evaluate DWARF
// location descriptions
// XXX TODO when added inside iosdbg, this will not create a string for my expression
// evaluator, will return a computed location
int decode_location_description(struct dwarf_locdesc *framebaselocdesc,
        struct dwarf_locdesc *locdesc, uint64_t pc, char **outbuffer,
        int64_t *resultout){
    char exprstr[1024] = {0};

    /* 512 to support extreme operands of DW_OP_pick */
    intptr_t stack[512] = {0};
    unsigned sp = 0;

    struct dwarf_locdesc *ld = locdesc;

    while(ld){
        Dwarf_Small op = ld->locdesc_op;
        Dwarf_Unsigned opd1 = ld->locdesc_opd1,
                       opd2 = ld->locdesc_opd2,
                       opd3 = ld->locdesc_opd3;

        char operatorbuf[64] = {0};
        char operandbuf[64] = {0};

        switch(op){
            case DW_OP_addr:
                {
                    //snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    snprintf(operandbuf, sizeof(operandbuf), "%#llx", opd1);

                    stack[++sp] = opd1;

                    break;
                }
            case DW_OP_deref:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), " %s", get_op_name(ld->locdesc_op));

                    intptr_t addr = stack[sp];
                    intptr_t data = 0;

                    if(read_memory_at_location(addr, &data, sizeof(data))){
                        concat(outbuffer, "warning: could not read memory at"
                                " %#llx for DW_OP_deref\n", addr);
                    }

                    stack[++sp] = data;

                    break;
                } 
            case DW_OP_const1u:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (uint8_t)opd1);
                    stack[++sp] = (uint8_t)opd1;
                    break;
                }
            case DW_OP_const1s:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (int8_t)opd1);
                    stack[++sp] = (int8_t)opd1;
                    break;
                }
            case DW_OP_const2u:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (uint16_t)opd1);
                    stack[++sp] = (uint16_t)opd1;
                    break;
                }
            case DW_OP_const2s:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (int16_t)opd1);
                    stack[++sp] = (int16_t)opd1;
                    break;
                }
            case DW_OP_const4u:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (uint32_t)opd1);
                    stack[++sp] = (uint32_t)opd1;
                    break;
                }
            case DW_OP_const4s:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (int32_t)opd1);
                    stack[++sp] = (int32_t)opd1;
                    break;
                }
            case DW_OP_const8u:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%lld", (uint64_t)opd1);
                    stack[++sp] = (uint64_t)opd1;
                    break;
                }
            case DW_OP_const8s:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%lld", (int64_t)opd1);
                    stack[++sp] = (int64_t)opd1;
                    break;
                }
            case DW_OP_constu:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%lld", (uint64_t)opd1);
                    stack[++sp] = (uint64_t)opd1;

                    break;
                }
            case DW_OP_consts:
                {
                    snprintf(operandbuf, sizeof(operandbuf), "%lld", (int64_t)opd1);
                    stack[++sp] = (int64_t)opd1;
                    break;
                }
            case DW_OP_dup:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    stack[sp + 1] = stack[sp];
                    sp++;
                    break;
                }
            case DW_OP_drop:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    sp--;
                    break;
                }
            case DW_OP_over:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    stack[sp + 1] = stack[sp - 1];
                    sp++;
                    break;
                }
            case DW_OP_pick:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    snprintf(operandbuf, sizeof(operandbuf), "%d", (uint8_t)opd1);

                    stack[sp + 1] = stack[sp - (uint8_t)opd1];
                    sp++;

                    break;
                }
            case DW_OP_swap:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    intptr_t t = stack[sp];
                    stack[sp] = stack[sp - 1];
                    stack[sp - 1] = t;

                    break;
                }
            case DW_OP_rot:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    intptr_t t = stack[sp];
                    stack[sp] = stack[sp - 1];
                    stack[sp - 1] = stack[sp - 2];
                    stack[sp - 2] = t;

                    break;
                }
            case DW_OP_xderef:
                {
                    break;
                }
            case DW_OP_abs:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp] = llabs(stack[sp]);

                    break;
                }
            case DW_OP_and:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] &= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_div:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] /= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_minus:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] -= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_mod:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] %= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_mul:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] *= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_neg:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp] = -stack[sp];

                    break;
                }
            case DW_OP_not:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp] = ~stack[sp];

                    break;
                }
            case DW_OP_or:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] |= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_plus:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] += stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_plus_uconst:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    snprintf(operandbuf, sizeof(operandbuf), " %ld", (intptr_t)opd1);

                    stack[sp] += (intptr_t)opd1;

                    break;
                }
            case DW_OP_shl:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] <<= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_shr:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] >>= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_shra:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] /= (1 << stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_xor:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] ^= stack[sp];
                    sp--;

                    break;
                }
            case DW_OP_bra:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    int16_t skip = (int16_t)opd1;
                    snprintf(operandbuf, sizeof(operandbuf), " %d", skip);

                    intptr_t val = stack[sp];

                    if(!val)
                        break;

                    if(skip < 0){
                        while(++skip)
                            ld = ld->locdesc_prev;
                    }
                    else{
                        for(int16_t i=0; i<skip; i++)
                            ld = ld->locdesc_next;
                    }

                    continue;
                }
            case DW_OP_eq:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] = (stack[sp - 1] == stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_ge:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] = (stack[sp - 1] >= stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_gt:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] = (stack[sp - 1] > stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_le:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] = (stack[sp - 1] <= stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_lt:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] = (stack[sp - 1] < stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_ne:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    stack[sp - 1] = (stack[sp - 1] != stack[sp]);
                    sp--;

                    break;
                }
            case DW_OP_skip:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));

                    int16_t skip = (int16_t)opd1;
                    snprintf(operandbuf, sizeof(operandbuf), " %d", skip);

                    if(skip < 0){
                        while(++skip)
                            ld = ld->locdesc_prev;
                    }
                    else{
                        for(int16_t i=0; i<skip; i++)
                            ld = ld->locdesc_next;
                    }

                    continue;
                }
            case DW_OP_lit0...DW_OP_lit31:
                {
                    stack[++sp] = op - DW_OP_lit0;
                    break;
                }
            case DW_OP_reg0...DW_OP_reg31:
                {
                    char *regname = get_register_name(op - DW_OP_reg0);
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", regname);
                    free(regname);

                    stack[++sp] = get_register_value(op - DW_OP_reg0);

                    break;
                }
            case DW_OP_breg0...DW_OP_breg31:
                {
                    char *regname = get_register_name(op - DW_OP_breg0);
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", regname);
                    free(regname);

                    snprintf(operandbuf, sizeof(operandbuf), "%+lld", (Dwarf_Signed)opd1);

                    stack[++sp] = get_register_value(op - DW_OP_breg0) +
                        (Dwarf_Signed)opd1;

                    break;
                }
            case DW_OP_regx:
                {
                    char *regname = get_register_name(opd1);
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", regname);
                    free(regname);

                    stack[++sp] = get_register_value(opd1);

                    break;
                }
            case DW_OP_fbreg:
                {
                    // XXX why is this here again?
                    memset(operatorbuf, 0, sizeof(operatorbuf));

                    /* Fetch what fbreg actually is */
                    char *fbregexpr = evaluate_frame_base(framebaselocdesc);

                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", fbregexpr);
                    snprintf(operandbuf, sizeof(operandbuf), "%+lld", opd1);

                    char *expr = NULL, *e = NULL;
                    concat(&expr, "%s%+lld", fbregexpr, (Dwarf_Signed)opd1);

                    free(fbregexpr);

                    intptr_t result = eval_expr(expr, &e);

                    if(e){
                        concat(outbuffer, "warning: could not evaluate"
                                " '%s' for DW_OP_fbreg\n", expr);
                    }

                    free(e);
                    free(expr);

                    stack[++sp] = result;

                    break;
                }
            case DW_OP_bregx:
                {
                    char *regname = get_register_name(opd1);
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", regname);
                    free(regname);
                    
                    snprintf(operandbuf, sizeof(operandbuf), "%+lld", (Dwarf_Signed)opd2);

                    stack[++sp] = get_register_value(opd1) + (Dwarf_Signed)opd2;

                    break;
                }
            case DW_OP_piece:
                {
                    concat(outbuffer, "DW_OP_piece not implemented\n");
                    break;
                }
            case DW_OP_deref_size:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    uint8_t deref_size = (uint8_t)opd1;

                    snprintf(operandbuf, sizeof(operandbuf), " %d", deref_size);

                    intptr_t addr = stack[sp];
                    intptr_t data = 0;

                    if(read_memory_at_location(addr, &data, sizeof(data))){
                        concat(outbuffer, "warning: could not read memory at"
                                " %#llx for DW_OP_deref_size\n", addr);
                    }

                    switch(deref_size){
                        case 1:
                            stack[++sp] = *(uint8_t *)&data;
                            break;
                        case 2:
                            stack[++sp] = *(uint16_t *)&data;
                            break;
                        case 3:
                            stack[++sp] = data & 0xffffff;
                            break;
                        case 4:
                            stack[++sp] = *(uint32_t *)&data;
                            break;
                        case 5:
                            stack[++sp] = data & 0xffffffffffULL;
                            break;
                        case 6:
                            stack[++sp] = data & 0xffffffffffffULL;
                            break;
                        case 7:
                            stack[++sp] = data & 0xffffffffffffffULL;
                            break;
                        case 8:
                            stack[++sp] = data;
                            break;
                        default:
                            {
                                concat(outbuffer, "warning: second operand for"
                                        " DW_OP_deref_size is 0\n");
                                break;
                            }
                    };

                    break;
                }
            case DW_OP_xderef_size:
                {
                    concat(outbuffer, "DW_OP_xderef_size not implemented\n");
                    break;
                }
            case DW_OP_nop:
                {
                    break;
                }
            case DW_OP_push_object_address:
                {
                    concat(outbuffer, "DW_OP_push_object_address not implemented\n");
                    break;
                }
            case DW_OP_call2:
                {
                    concat(outbuffer, "DW_OP_call2 not implemented\n");
                    break;
                }
            case DW_OP_call4:
                {
                    concat(outbuffer, "DW_OP_call4 not implemented\n");
                    break;
                }
            case DW_OP_call_ref:
                {
                    concat(outbuffer, "DW_OP_call_ref not implemented\n");
                    break;
                }
            case DW_OP_form_tls_address:
                {
                    concat(outbuffer, "DW_OP_form_tls_address not implemented\n");
                    break;
                }
            case DW_OP_call_frame_cfa:
                {
                    concat(outbuffer, "DW_OP_call_frame_cfa not implemented\n");
                    break;
                }
            case DW_OP_bit_piece:
                {
                    concat(outbuffer, "DW_OP_bit_piece not implemented\n");
                    break;
                }
            case DW_OP_implicit_value:
                {
                    concat(outbuffer, "DW_OP_implicit_value not implemented\n");
                    break;
                }
            case DW_OP_stack_value:
                {
                    snprintf(operatorbuf, sizeof(operatorbuf), "%s", get_op_name(ld->locdesc_op));
                    goto done;
                }
            default:
                {
                    concat(outbuffer, "Unhanded DWARF operation %#x\n", op);
                    break;
                }
        };

        strcat(exprstr, operatorbuf);
        strcat(exprstr, operandbuf);

        ld = ld->locdesc_next;
        continue;
done:
        break;
    }

    *resultout = stack[sp];

    return 0;
}

void *get_next_location_description(struct dwarf_locdesc *ld){
    if(!ld)
        return NULL;

    return ld->locdesc_next;
}

void initialize_die_loclists(struct dwarf_locdesc ***locdescs,
        Dwarf_Unsigned lcount){
    *locdescs = calloc(lcount, sizeof(struct dwarf_locdesc));
}

void loc_free(struct dwarf_locdesc *locdesc){
    struct dwarf_locdesc *current = locdesc;

    while(current){
        struct dwarf_locdesc *f = current;
        current = current->locdesc_next;
        free(f);
    }
}
