#include "cpu/exec.h"
#include "cpu/rtl.h"

/* shared by all helper functions */
DecodeInfo decoding;
rtlreg_t t0, t1, t2, t3, at;

void decoding_set_jmp(bool is_jmp) {
  decoding.is_jmp = is_jmp;
}


#define make_DopHelper(name) void concat(decode_op_, name) (vaddr_t *eip, Operand *op, bool load_val)

/* Refer to Appendix A in i386 manual for the explanations of these abbreviations */

/**
Codes for Addressing Method
A Direct address; the instruction has no modR/M byte; the address of the operand is encoded in the instruction; no base register, index register, or scaling factor can be applied; e.g., far JMP (EA).
C The reg field of the modR/M byte selects a control register; e.g., MOV (0F20, 0F22).
D The reg field of the modR/M byte selects a debug register; e.g., MOV (0F21,0F23).
E A modR/M byte follows the opcode and specifies the operand. The operand is either a general register or a memory address. If it is a memory address, the address is computed from a segment register and any of the following values: a base register, an index register, a scaling factor, a displacement.
F Flags Register.
G The reg field of the modR/M byte selects a general register; e.g., ADD (00).
I Immediate data. The value of the operand is encoded in subsequent bytes of the instruction.
J The instruction contains a relative offset to be added to the instruction pointer register; e.g., JMP short, LOOP.
M The modR/M byte may refer only to memory; e.g., BOUND, LES, LDS, LSS, LFS, LGS.
O The instruction has no modR/M byte; the offset of the operand is coded as a word or double word (depending on address size attribute) in the instruction. No base register, index register, or scaling factor can be applied; e.g., MOV (A0-A3)
R The mod field of the modR/M byte may refer only to a general register; e.g., MOV (0F20-0F24, 0F26).
S The reg field of the modR/M byte selects a segment register; e.g., MOV (8C,8E).
T The reg field of the modR/M byte selects a test register; e.g., MOV (0F24,0F26).
X Memory addressed by DS:SI; e.g., MOVS, COMPS, OUTS, LODS, SCAS.
Y Memory addressed by ES:DI; e.g., MOVS, CMPS, INS, STOS. Codes for Operant Type a Two one-word operands in memory or two double-word operands in memory, depending on operand size attribute (used only by BOUND).
b Byte (regardless of operand size attribute)
c Byte or word, depending on operand size attribute.
d Double word (regardless of operand size attribute)
p 32-bit or 48-bit pointer, depending on operand size attribute.
s Six-byte pseudo-descriptor
v Word or double word, depending on operand size attribute.
w Word (regardless of operand size attribute)

Register Codes
When an operand is a specific register encoded in the opcode, the register
is identified by its name; e.g., AX, CL, or ESI. The name of the register
indicates whether the register is 32-, 16-, or 8-bits wide. A register
identifier of the form eXX is used when the width of the register depends on
the operand size attribute; for example, eAX indicates that the AX register
is used when the operand size attribute is 16 and the EAX register is used
when the operand size attribute is 32
 */

// byte word double word immediate
/* Ib, Iv */
static inline make_DopHelper(I) {
  /* eip here is pointing to the immediate */
  op->type = OP_TYPE_IMM;
  op->imm = instr_fetch(eip, op->width);
  rtl_li(&op->val, op->imm);
#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "$0x%x", op->imm);
  // printf("load imm %s with width = %d\n", op->str, op->width);
#endif
}

/* I386 manual does not contain this abbreviation, but it is different from
 * the one above from the view of implementation. So we use another helper
 * function to decode it.
 */
/* sign immediate */
static inline make_DopHelper(SI) {
  // WARN I can not understand why op->width can not eqaul to 2
  assert(op->width == 1 || op->width == 4);


  op->type = OP_TYPE_IMM;

  /* TODO: Use instr_fetch() to read `op->width' bytes of memory
   * pointed by `eip'. Interpret the result as a signed immediate,
   * and assign it to op->simm.
   *
   * op->simm = ???
   */
  op->simm = instr_fetch(eip, op->width);
  // WARN maybe the following code should use rtl language to do it 
  if(op->width == 1){
    int mask = 1 << (op->width * 8 - 1);
    if(op->simm & mask){
      op->simm = (((uint32_t)-1) << (op->width * 8)) | op->simm;
    }
  }
  // printf("get immm %x width = %d\n", op->simm, op->width);
  rtl_li(&op->val, op->simm);

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "$0x%x", op->simm);
  // printf("load immm %s with width = %d\n", op->str, op->width);
#endif
}


// read the reax to oprand
// oh shit, should we change the eax to  
/* I386 manual does not contain this abbreviation.
 * It is convenient to merge them into a single helper function.
 */
/* AL/eAX */
static inline make_DopHelper(a) {
  op->type = OP_TYPE_REG;
  op->reg = R_EAX;
  if (load_val) {
    rtl_lr(&op->val, R_EAX, op->width);
  }

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "%%%s", reg_name(R_EAX, op->width));
#endif
}

/* This helper function is use to decode register encoded in the opcode. */
/* XX: AL, AH, BL, BH, CL, CH, DL, DH
 * eXX: eAX, eCX, eDX, eBX, eSP, eBP, eSI, eDI
 */
static inline make_DopHelper(r) {
  op->type = OP_TYPE_REG;
  op->reg = decoding.opcode & 0x7; // opcode determine the reg to use
  if (load_val) {
    rtl_lr(&op->val, op->reg, op->width);
  }

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "%%%s", reg_name(op->reg, op->width));
#endif
}

/* I386 manual does not contain this abbreviation.
 * We decode everything of modR/M byte by one time.
 */
/* Eb, Ew, Ev
 * Gb, Gv
 * Cd,
 * M
 * Rd
 * Sw
 */
static inline void decode_op_rm(vaddr_t *eip, Operand *rm, bool load_rm_val, Operand *reg, bool load_reg_val) {
  read_ModR_M(eip, rm, load_rm_val, reg, load_reg_val);
}

// A0		MOV AL,moffs8*		Move byte at (seg:offset) to AL.
/* Ob, Ov */
static inline make_DopHelper(O) {
  op->type = OP_TYPE_MEM;
  rtl_li(&op->addr, instr_fetch(eip, 4));
  if (load_val) {
    rtl_lm(&op->val, &op->addr, op->width);
  }

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "0x%x", op->addr);
#endif
}

/* Eb <- Gb
 * Ev <- Gv
 */
make_DHelper(G2E) {
  decode_op_rm(eip, id_dest, true, id_src, true);
}

make_DHelper(mov_G2E) {
  decode_op_rm(eip, id_dest, false, id_src, true);
}


/* Gb <- Eb
 * Gv <- Ev
 */
make_DHelper(E2G) {
  decode_op_rm(eip, id_src, true, id_dest, true);
}

make_DHelper(mov_E2G) {
  decode_op_rm(eip, id_src, true, id_dest, false);
}

// send the address to register
// get the reg and get the address
// the first is reg and second is the reg
make_DHelper(lea_M2G) {
  // printf("decode wtih lea_M2G\n");
  decode_op_rm(eip, id_src, false, id_dest, false);
}

/* AL <- Ib
 * eAX <- Iv
 */
make_DHelper(I2a) {
  decode_op_a(eip, id_dest, true);
  decode_op_I(eip, id_src, true);
}

/* Gv <- EvIb
 * Gv <- EvIv
 * use for imul */
make_DHelper(I_E2G) {
  decode_op_rm(eip, id_src2, true, id_dest, false);
  decode_op_I(eip, id_src, true);
}


/* Eb <- Ib
 * Ev <- Iv
 */
make_DHelper(I2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  decode_op_I(eip, id_src, true);
}

make_DHelper(mov_I2E) {
  decode_op_rm(eip, id_dest, false, NULL, false);
  decode_op_I(eip, id_src, true);
}

/* XX <- Ib
 * eXX <- Iv
 */
make_DHelper(I2r) {
  decode_op_r(eip, id_dest, true);
  decode_op_I(eip, id_src, true);
}

make_DHelper(mov_I2r) {
  decode_op_r(eip, id_dest, false); // set the destination
  decode_op_I(eip, id_src, true); 
}

/* used by unary operations */
make_DHelper(I) {
  decode_op_I(eip, id_dest, true);
}

// load the selected reg to dest operand
make_DHelper(r) {
  decode_op_r(eip, id_dest, true);
}

make_DHelper(E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
}

make_DHelper(setcc_E) {
  decode_op_rm(eip, id_dest, false, NULL, false);
}


make_DHelper(gp7_E) {
  decode_op_rm(eip, id_dest, false, NULL, false);
}

// other instruction in this group only have one oprand
// except for the test, so it should read another number
/* used by test in group3 */
make_DHelper(test_I) {
  decode_op_I(eip, id_src, true);
}

make_DHelper(SI2E) {
  assert(id_dest->width == 2 || id_dest->width == 4);
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->width = 1;
  decode_op_SI(eip, id_src, true);
  if (id_dest->width == 2) {
    id_src->val &= 0xffff;
  }
}

make_DHelper(SI_E2G) {
  assert(id_dest->width == 2 || id_dest->width == 4);
  decode_op_rm(eip, id_src2, true, id_dest, false);
  id_src->width = 1;
  decode_op_SI(eip, id_src, true);
  if (id_dest->width == 2) {
    id_src->val &= 0xffff;
  }
}


make_DHelper(gp2_1_E) {
  // printf("decode with gp2_1_E\n");
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->type = OP_TYPE_IMM;
  id_src->imm = 1;
  rtl_li(&id_src->val, 1);
#ifdef DEBUG
  sprintf(id_src->str, "$1");
#endif
}

make_DHelper(gp2_cl2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_CL;
  rtl_lr(&id_src->val, R_CL, 1);
#ifdef DEBUG
  sprintf(id_src->str, "%%cl");
#endif
}

make_DHelper(gp2_Ib2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->width = 1;
  decode_op_I(eip, id_src, true);
}

/* Ev <- GvIb
 * use for shld/shrd */
make_DHelper(Ib_G2E) {
  decode_op_rm(eip, id_dest, true, id_src2, true);
  id_src->width = 1;
  decode_op_I(eip, id_src, true);
}

/* Ev <- GvCL
 * use for shld/shrd */
make_DHelper(cl_G2E) {
  decode_op_rm(eip, id_dest, true, id_src2, true);
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_CL;
  rtl_lr(&id_src->val, R_CL, 1);
#ifdef DEBUG
  sprintf(id_src->str, "%%cl");
#endif
}

// it seems that only 32bit version(mov ax, moffs) supported
make_DHelper(O2a) {
  decode_op_O(eip, id_src, true);
  decode_op_a(eip, id_dest, false);
}

make_DHelper(a2O) {
  decode_op_a(eip, id_src, true);
  decode_op_O(eip, id_dest, false);
}

make_DHelper(J) {
  decode_op_SI(eip, id_dest, false);
  // the target address can be computed in the decode stage
  decoding.jmp_eip = id_dest->simm + *eip;
	// printf("jump to [%x]\n", decoding.jmp_eip);
}

make_DHelper(push_SI) {
  decode_op_SI(eip, id_dest, true);
}

make_DHelper(in_I2a) {
  id_src->width = 1;
  decode_op_I(eip, id_src, true);
  decode_op_a(eip, id_dest, false);
}

make_DHelper(in_dx2a) {
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_DX;
  rtl_lr(&id_src->val, R_DX, 2);
#ifdef DEBUG
  sprintf(id_src->str, "(%%dx)");
#endif

  decode_op_a(eip, id_dest, false);
}

make_DHelper(out_a2I) {
  decode_op_a(eip, id_src, true);
  id_dest->width = 1;
  decode_op_I(eip, id_dest, true);
}

make_DHelper(out_a2dx) {
  decode_op_a(eip, id_src, true);

  id_dest->type = OP_TYPE_REG;
  id_dest->reg = R_DX;
  rtl_lr(&id_dest->val, R_DX, 2);
#ifdef DEBUG
  sprintf(id_dest->str, "(%%dx)");
#endif
}

void operand_write(Operand *op, rtlreg_t* src) {
  if (op->type == OP_TYPE_REG) { rtl_sr(op->reg, src, op->width); }
  else if (op->type == OP_TYPE_MEM) { rtl_sm(&op->addr, src, op->width); }
  else { assert(0); }
}
