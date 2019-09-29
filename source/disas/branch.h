#ifndef _BRANCH_H_
#define _BRANCH_H_

enum bikind {
    COND_BRANCH_IMMEDIATE = 0,
    UNCOND_BRANCH_REGISTER,
    UNCOND_BRANCH_IMMEDIATE,
    COMP_AND_BRANCH_IMMEDIATE,
    TEST_AND_BRANCH_IMMEDIATE,
    UNKNOWN_KIND
};

enum bicond {
    EQ = 0, NE,
    CS, CC,
    MI, PL,
    VS, VC,
    HI, LS,
    GE, LT,
    GT, LE,
    AL,
    UNKNOWN_COND
};

enum birn {
    X0 = 0, X1, X2, X3, X4, X5, X6,
    X7, X8, X9, X10, X11, X12,
    X13, X14, X15, X16, X17, X18,
    X19, X20, X21, X22, X23, X24,
    X25, X26, X27, X28, X29, X30, XZR,
    W0, W1, W2, W3, W4, W5, W6,
    W7, W8, W9, W10, W11, W12,
    W13, W14, W15, W16, W17, W18,
    W19, W20, W21, W22, W23, W24,
    W25, W26, W27, W28, W29, W30, WZR,
    NONE
};

static const char *const BIRN_TABLE[] = {
    "X0",  "X1",  "X2",  "X3",  "X4",  "X5",  "X6",
    "X7",  "X8",  "X9",  "X10",  "X11",  "X12",
    "X13",  "X14",  "X15",  "X16",  "X17",  "X18",
    "X19",  "X20",  "X21",  "X22",  "X23",  "X24",
    "X25",  "X26",  "X27",  "X28",  "X29",  "X30",  "XZR",
    "W0",  "W1",  "W2",  "W3",  "W4",  "W5",  "W6",
    "W7",  "W8",  "W9",  "W10",  "W11",  "W12",
    "W13",  "W14",  "W15",  "W16",  "W17",  "W18",
    "W19",  "W20",  "W21",  "W22",  "W23",  "W24",
    "W25",  "W26",  "W27",  "W28",  "W29",  "W30",  "WZR",  "NONE"
};

struct branchinfo {
    enum bikind kind;
    
    int conditional;
    enum bicond cond;

    int imm;

    enum birn rn;

    int is_subroutine_call;
};

int is_branch(unsigned int, struct branchinfo *);

#endif
