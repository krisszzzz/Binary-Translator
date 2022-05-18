#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <log.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <label_table.h>


#define PAGESIZE 4096          // linux pagesize
#define HOST_MEMORY_COUNT 993  // Used memory in host assembler
#define TRANSLATE_PUSH_SIZE 23 // Size of opcode of one push register,
                               // Need to correct call
#define UNKNOWN 0              // Define to mark unknown yet label

#define MIN_DST_CODE_SIZE 2 << 14 // Size of execution buffer (Yea in this version it is fixed)

#define BYTE(val) val * 8         // Some usefull defines to decrease amount of code
#define OPSIZE(op_code_name) SIZEOF_##op_code_name


//+================| WORD SIZES |==================+
enum WORD_SIZE : u_int64_t {
        DWORD   = 4,
        QWORD   = 8,
        XMMWORD = 16,
};

//-================================================-

//+===============| COMPARISONS |==================+
enum VCMMPD_COMPARISONS_CODE : u_int64_t {
        EQUAL = 0,
        LESS = 17,
        GREATER = 30
};

//-================================================-


//+===========| MASK FOR XMM REGISTER |============+
enum REG_MASK : u_int64_t {
        XMM0_EXTEND = 0x44,
        XMM5_BASE   = 0x2C,
        XMM5_EXTEND = 0x6C
};
        
//-================================================-


//+============| TRANSLATION ERRORS |==============+

enum TRANSLATION_ERROR {
  CVT_ERROR = -0xDED,
  MALLOC_ERROR,
  FILE_OPENING_ERROR,
  UNKNOWN_FILE 
};


//+===========| HOST ASSEMBLER OPCODES |============+

enum HOST_STACK_OP_CODES { // C++11
  PUSH  = 1,
  POP   = 2,
  IN    = 3,
  OUT   = 4,
  MUL   = 5,
  ADD   = 6,
  SUB   = 7,
  DIV   = 8,
  HLT   = 10,
  JB    = 11,
  CALL  = 12,
  RET   = 13,
  JA    = 14,
  JMP   = 15,
  SQRT  = 16,
  JE    = 17,
  POPM  = 2  | 0x70,
  POPR  = 2  | 0x60,
  POPRM = 2  | 0x35, // Pop to memory with register index
  PUSHM = 1  | 0x70,
  PUSHR = 1  | 0x60,
  PUSHRM = 1 | 0x35

};

enum HOST_ASSEMBLY_REG_ID : u_int64_t {
  AX = 1,
  BX,
  CX,
  DX
};


//-===============================================-

//+=============| NATIVE ASSEMBLER |==============+

enum X86_ASSEMBLY_OPCODES : u_int64_t {

  //+===============| REG_IDS |===================+
  //
  RAX = 0,
  RBX = 3, //
  RCX = 1,
  RDX = 2,

  XMM0 = RAX,
  XMM1 = RCX,
  XMM2 = RDX,
  XMM3 = RBX,
  XMM4,

  //-===============================================-

  //+================| OP_CODES |===================+

  // NOT THAT ALL OP_CODES REVERSED BECAUSE OF LITTLE ENDIAN
          /*
                          -------------------------+-+
                         // .------------------v   v v
                        ,, .  vmovq xmm(0-5), [rsp - 8]     | XMM Encoding 
                        || | +------^                       | xmm0 = 0x44 = 01000100b
                        ^^ ^ ^                              | xmm1 = 0x4C = 01010100b  ...  */
  VMOVQ_XMM_RSP_IMM = 0x0024007EFAC5, //                                         ^
          /*                                                                     |
                      +---------+                                                |
                     /   +------+---------+    | XMM Encoding:                   |
                    .  /        |         |    | xmm0 = 0x4                      |
                    | .         V         v    | xmm1 = 0xC ..                   |
                    | | vmovq [rsp], xmm(0-5)                                    |
                    ^ ^                                                          |    */
  VMOVQ_RSP_XMM = 0x2400D6F9C5, //                                               |
          /*                                                                     |
                     +---------+                                                 |
                    /          |                                                 |
                   .           V                                                 |
                   | sub rsp, imm                                                |
                   ^                                                             |    */
  SUB_RSP_IMM  = 0x00EC8348, //                                                  |
  ADD_RSP_IMM  = 0x00C48348, //                                                  |
          /*                                                                     |
                         .----------------+     +---------< XMM Encoding: see  +++
                        /  --------+      |     |                                |
                        | /        V      V     V                                |
                        | | vmovq [rsp + 00], xmm(0-4)                           |
                        | |  .----------------^                                  |
                        V V  V                                                   |     */
  VMOVQ_RSP_IMM_XMM = 0x002400D6F9C5,  //                                        |
          /*                  ^ ^ ^                                              |
                              | | |                                              |
                              vmovq code                                         |
                                                                                 |
                           .----------------+     +------------< Encoding: See  ++
                          /   .-------+     |     |
                        ./ ./         V     V     V
                        |  |  addsd [rsp + 00], xmm(0-4)
                        |  |  .------------------^
                        |  | /                                                          */
  ADDSD_XMM_RSP_IMM = 0x002400580FF2,
          /*                  ^ ^ ^
                              | | |
                              addsd                                                     */
  SUBSD_XMM_RSP_IMM = 0x0024005C0FF2,
  MULSD_XMM_RSP_IMM = 0x002400590FF2,
  DIVSD_XMM_RSP_IMM = 0x0024005E0FF2,

          /*              +-+-+-+------------+
                         / / / /             |
                        . . . .      +-+-+-+-+-+-+-+-+
                        | | | |      V V V V V V V V V
                        ^ ^ ^ ^      vsqrtpd xmm0, xmm0                                  */
  VSQRTPD_XMM0_XMM0 = 0xC051F9C5,
          /*
            Used to memory reference
                        
                             +-----------------------+
                            /                        |
                           .                         V
                           |     vmovq xmm5, [r13 + imm]
                           ^                                                             */
  VMOVQ_XMM5_R13_B_IMM = 0x006D7E7AC1C4,
          /*
            Identical, but the imm is double word (4 bytes)
                                                                                         */
  VMOVQ_XMM5_R13_D_IMM = 0xAD7E7AC1C4,
         
  VMOVQ_R13_B_IMM_XMM5 = 0x006DD679C1C4,

          /* !NOTE!: SIZEOF VMOVQ_R13_D_IMM_XMM is 9 bytes,
                     so i have to split it, and cannot use the last byte.
                     It is suit for my case
                                                                                         */
             
  VMOVQ_R13_D_IMM_XMM5  = 0xADD679C1C4,
          /* Relative jump
                   +-+-+-+--------
                  / / / /        |
                 . . . .         V
                 | | | |    jmp rel32                                                      
                 ^ ^ ^ ^                                                                 */
  JMP_REL32  = 0x00000000E9,
  JE_REL32   = 0x00000000840F,          
          /*                  +-------------------------+
                             /                          |
                            .                           V
                            |  vcmppd xmm5, xmm0, xmm5, 0 - Comparison mod
                            ^                                                            */
  VCMPPD_XMM5_XMM0_XMM5 = 0x00EDC2F9C5,
          /*       +-+-+-+-----------+
                  / / / /            |
                 . . . .       +-+-+-+-+-+
                 | | | |       | | | | | | 
                 | | | |       V V V V V V 
                 | | | |       cmp r14d, 3    - Note that r14d contain the mask, and the mask 3
                 | | | |                        mean that for first double and second double in xmm
                 | | | |                        register the comparison condition is true
                 | | | |                        Because we don't use the hight half of xmm register
                 | | | |                        The second bit, ,will be always set. So this is Why we use
                 ^ ^ ^ ^                        number 3, not 1                           */
  CMP_R14D_3 = 0x03FE8341,
  CMP_R14D_1 = 0x01FE8341,


  LEA_RDI_RSP_16 = 0x10247C8D48,
          /*
                   +-+-+-+----------------+
                  / / / /                 |
                 . . . .                  V
                 | | | |         mov edi, 0
                 ^ ^ ^ ^                                                                   */
  MOV_EDI_0  = 0x00000000BF,        
          
          /*
                             +-+-+-+-+-----------+
                            / / / / /            |
                           . . . . .   +-+-+-+-+-+-+-+-+-+
                           | | | | |   v V v v v v v v v v 
                           ^ ^ ^ ^ ^   movmskpd r14d, xmm5                                  */
 MOVMSKPD_R14D_XMM5    = 0xF5500F4466,
          /*
             To call scanf
                             +-+-+-----------+
                            / / /            |
                           . . .       +-+-+-+-+-+
                           | | |       V V V V V V
                           ^ ^ ^       mov rdi, rsp                                          */
 MOV_RDI_RSP           = 0xE78948,
          
 NATIVE_RET        = 0xC3,

          /*            .-.-.-.-----------< Rel 32 immediate address
                        | | | |
                        v v v v                                                               */
  NATIVE_CALL       = 0x00000000E8,        
          
  MOV_R15_RSP       = 0xE78949, // Full opcode because instruction is used once 
  MOV_RSP_R15       = 0xFC894C,
  MOV_R14           = 0xBE49,
  MOV_TO_STACK_R14  = 0x2434894C,
  MOV_R13           = 0xBD49,

  //-===============================================-
};





enum X86_ASSEMBLY_OPCODES_SIZE {
  SIZEOF_VMOVQ_RSP_XMM         = 5,
  SIZEOF_VMOVQ_RSP_IMM_XMM     = 6,
  SIZEOF_SUB_RSP_IMM           = 4,
  SIZEOF_ADD_RSP_IMM           = 4,
  SIZEOF_ADDSD_XMM_RSP_IMM     = 6,
  SIZEOF_MOV_R15_RSP           = 3,
  SIZEOF_MOV_RSP_R15           = 3,
  SIZEOF_NATIVE_CALL           = 5,
  SIZEOF_MOV_R14               = 2,
  SIZEOF_MOV_TO_STACK_R14      = 4,
  SIZEOF_VMOVQ_XMM_RSP_IMM     = 6,
  SIZEOF_VMOVQ_XMM5_R13_B_IMM  = 6,
  SIZEOF_VMOVQ_XMM5_R13_D_IMM  = 5,
  SIZEOF_MOV_R13               = 2,
  SIZEOF_JMP_REL32             = 5,
  SIZEOF_JE_REL32              = 6,
  SIZEOF_VCMPPD_XMM5_XMM0_XMM5 = 5,
  SIZEOF_MOVMSKPD_R14D_XMM5    = 5,
  SIZEOF_CMP_R14D_1            = 4,
  SIZEOF_CMP_R14D_3            = 4,
  SIZEOF_MOV_RDI_RSP           = 3,
  SIZEOF_MOV_EDI_0             = 5,
  SIZEOF_VSQRTPD_XMM0_XMM0     = 4,
  SIZEOF_LEA_RDI_RSP_16        = 5,
  SIZEOF_VMOVQ_R13_B_IMM_XMM5  = 6,
  SIZEOF_VMOVQ_R13_D_IMM_XMM5  = 5,
  SIZEOF_RET                   = 1
  
};

//-===============================================-

struct opcode
{
        u_int64_t code;
        int       size;
};
 
union cvt_u_int64_t_int {
                int       rel_addr;
                u_int64_t extended_address;                 // Zero extended 
};

struct assembly_code
{
        char*  code;
        int    position;
        size_t size;
};


int assembly_code_init(assembly_code* const __restrict self,
                       const size_t                    size)
        __attribute__((nonnull(1)));

int load_code(const char*    const __restrict src_file_name,
              assembly_code* const __restrict src_code_save)
        __attribute__((nonnull(1,2)));


//+=========================| FUNCTIONS DECLARATIONS |===============================+

void make_label_table(assembly_code* const __restrict src_code,
                      label_table*   const __restrict table)
        __attribute__((nonnull(1,2)));

int assembly_code_aligned_init(assembly_code* const __restrict self,
                               const size_t                    alignment,
                               const size_t                    size)
        __attribute__((nonnull(1)));


void label_setting(assembly_code* const __restrict dst_code,
                   label_table* const   __restrict table,
                   const int                       indx,
                   const size_t                    code_pos,
                   const int                       label_pos)
        __attribute__((nonnull(1,2)));

int save_jmp_n_call_rel32(assembly_code* const __restrict dst_code,
                          const size_t                    code_pos)
        __attribute__((nonnull(1)));


opcode translate_jmp_n_call(assembly_code* const __restrict dst_code,
                            const int                       jmp_code)
       __attribute__((nonnull(1), always_inline));


void execute_start(char* const __restrict execution_buffer,
                   const int              time_flag)
       __attribute__((nonnull(1)));

int translation_start(const char *const __restrict    src_file_name,
                      assembly_code *const __restrict dst_buffer,
                      const int                       time_flag)
       __attribute__((nonnull(1,2)));

int load_src_assembly_code(const char *const __restrict    src_file_name,
                           assembly_code *const __restrict src_code_save)
        __attribute__((nonnull(1,2)));

void command_line_handler(int argc, char* argv[])
        __attribute__((nonnull(2)));

extern "C" int double_printf(double* value);
extern "C" int double_scanf (double* value);


//+======================| INLINE FUNCTIONS DECLARATIONS |===========================+


inline void write_command(assembly_code* const __restrict  dst_code,
                          opcode                           operation_code)
        __attribute__((always_inline, nonnull(1)));


inline void translate_load_rsp(assembly_code* const __restrict dst_node)
        __attribute__((always_inline, nonnull(1)));


inline u_int64_t cvt_host_reg_id_to_native(const int host_reg_id,
                                           const u_int64_t suffix,
                                           const u_int64_t offset)
       __attribute__((always_inline));


inline void translate_push(assembly_code* const __restrict dst_code,
                           const u_int64_t                 data)
        __attribute__((nonnull(1), always_inline));

inline void translate_push_r(assembly_code* const __restrict dst_code,
                             const int                       reg_id)
        __attribute__((nonnull(1), always_inline));


inline void translate_cycle(assembly_code* const __restrict dst_code,
                            label_table* const __restrict   table,
                            const int                       label_pos,
                            const int                       jmp_pos,
                            const int                       jmp_code)
        __attribute__((nonnull(1,2), always_inline));

inline void translate_jmp(assembly_code* const __restrict dst_code,
                          label_table*   const __restrict table,
                          const int label_pos,
                          const int jmp_pos)
        __attribute__((nonnull(1,2), always_inline));

inline void translate_ahead_jmp_n_call(assembly_code* const __restrict dst_code,
                                       label_table*   const __restrict table,
                                       const int                       label_pos,
                                       const int                       jmp_n_call_pos,
                                       const int                       jmp_n_call_code)
        __attribute__((nonnull(1,2), always_inline));


inline void jmp_n_call_handler(assembly_code* const __restrict dst_code,
                               label_table*   const __restrict table,
                               const int                       label_pos,
                               const int                       jmp_n_call_pos,
                               const int                       jmp_n_call_code)
        __attribute__((nonnull(1,2), always_inline));

inline void translate_save_rsp(assembly_code* const __restrict dst_node)
        __attribute__((nonnull(1), always_inline));


inline void translate_stdout(assembly_code* const __restrict dst_buffer)
        __attribute__((always_inline, nonnull(1)));



inline void translate_two_pop_for_cmp(assembly_code* const __restrict dst_code,
                                      const int                       jmp_code)
       __attribute__((nonnull(1), always_inline));
        

inline void translate_ret(assembly_code* const __restrict dst_code)
        __attribute__((nonnull(1), always_inline));



inline void translate_arithmetic_op(assembly_code* const __restrict dst_code,
                                    const int                       op_id)
        __attribute__((nonnull(1), always_inline));
        
//-===========================================================================-


