/**
 *  @file translator.h
 *  @brief The header of translator, containing all used functions in JIT
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <log.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <label_table.h>



#define PAGESIZE 4096              /**< Default linux page size. Needed to mprotect correct work */
#define HOST_MEMORY_COUNT 993      /**< All memory indexes that used in host assembler */
#define TRANSLATE_PUSH_SIZE 23     /**< Size of opcode of one push register, needed to correct call */
#define UNKNOWN 0                  /**< Define to mark unknown yet label */
#define MIN_DST_CODE_SIZE 2 << 14  /**< Size of execution buffer (currently this is fixed value) */

/** @def BYTE(val)
 *  @def OPSIZE(op_code_name)
 *  @brief Some usefull defines to decrease amount of code */

#define BYTE(val) val * 8
#define OPSIZE(op_code_name) SIZEOF_##op_code_name


//+================| WORD SIZES |==================+

/** Enum containing word sizes */

enum WORD_SIZE : u_int64_t {
        DWORD   = 4,
        QWORD   = 8,
        XMMWORD = 16,
};

//-================================================-

//+===============| COMPARISONS |==================+

/** Enum containing comparison "code" for VCMPPD instruction */

enum VCMPPD_COMPARISONS_CODE : u_int64_t {
        EQUAL = 0,
        LESS = 17,
        GREATER = 30
};

//-================================================-


//+===========| MASK FOR XMM REGISTER |============+

/** Enum containing register code in x86 Assembler */

enum REG_MASK : u_int64_t {
        XMM0_EXTEND = 0x44,
        XMM5_BASE   = 0x2C,
        XMM5_EXTEND = 0x6C
};
        
//-================================================-


//+============| TRANSLATION ERRORS |==============+

/** Enum for error handling */

enum TRANSLATION_ERROR {
  CVT_ERROR = -0xDED,
  MALLOC_ERROR,
  FILE_OPENING_ERROR,
  UNKNOWN_FILE 
};


//+===========| HOST ASSEMBLER OPCODES |============+

/** Opcodes for my CPU emulator (soft-CPU) */

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

/** My own register "opcodes" (masks to be exact) */

enum HOST_ASSEMBLY_REG_ID : u_int64_t {
  AX = 1,
  BX,
  CX,
  DX
};


//-===============================================-

//+=============| NATIVE ASSEMBLER |==============+

/** Enum containing all x86 opcodes               */

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


/** Enum contating the size (in bytes) of each x86 opcodes, used by program  */

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

/** Structure used to simplify writing opcodes  */
struct opcode
{
        u_int64_t code; /**< Opcode, that should be written in the buffer */
        int       size; /**< Size of this opcode */
};

/** Union to zero-extended conversation from int to u_int64_t */
union cvt_u_int64_t_int {
        int       rel_addr;         /**< Value to convert. The name of the variable is because it
                                     * is used primarily for address translation. */
        u_int64_t extended_address; /**< Zero extended value. */
};


/** Structure containing the assembled code and host (source) assembler code  */
struct assembly_code
{
        char*  code;     /**< Buffer to contation code */
        int    position; /**< position of current opcode in buffer */
        size_t size;     /**< size of buffer */
};

/** @brief function to initialize assembly_code structure
 *  @param self object to initialize
 *  @param size size of alocated memory for structure field self->buffer 
 *  @return return MALLOC_ERROR (see TRANSLATION_ERROR) if allocation is failed. Return 0 if succeed.
 *  @see TRANSLATION_ERROR
 */
int assembly_code_init(assembly_code* const __restrict self,
                       const size_t                    size)
        __attribute__((nonnull(1)));

/** @brief function to read code from file and load it to struct field of assembly_code
 *  @param src_file_name name of source file to read
 *  @param src_code_save object of structure assembly_code to save source code
 *  @return FILE_OPENING_ERROR - if cannot open the file. MALLOC_ERROR - if memory allocation error
 *  detected, else return 0.
 */
int load_code(const char*    const __restrict src_file_name,
              assembly_code* const __restrict src_code_save)
        __attribute__((nonnull(1,2)));


//+=========================| FUNCTIONS DECLARATIONS |===============================+

/** @brief function for making so-called label_table - structure (hash table) contating all info about labels (such as position in source code)
 *  @param src_code source code where labels will be looked up
 *  @param table object of label_table to save info about labels
 */

void make_label_table(assembly_code* const __restrict src_code,
                      label_table*   const __restrict table)
        __attribute__((nonnull(1,2)));

/** @brief same as assembly_code_init(), but the memory allocated using aligned_alloc() function
 *  @param self object to initialize
 *  @param alignment alignment of allocated memory
 *  @param size size of alocated memory for structure field self->buffer 
 *  @return return MALLOC_ERROR (see TRANSLATION_ERROR) if allocation is failed. Return 0 if succeed.
 *  @see TRANSLATION_ERROR
 */

int assembly_code_aligned_init(assembly_code* const __restrict self,
                               const size_t                    alignment,
                               const size_t                    size)
        __attribute__((nonnull(1)));


/** @brief link label with appropriate jmp or call
 *  @param dst_code buffer, where translated code will be injected
 *  @param table label_table structure, and for table the make_label_table() function must be called
 *  @param search_indx index in table->elem array. When the make_label_table() function was called,
 *  labels were added to the label_table structure. In the future, when translating the code, the
 *  hash table is searched using the label_table_search_by_label() function, that is, in fact, we
 *  check whether the position of the code that we are translating is a label or not. This index
 *  is responsible for the position of the label in the label_table structure.
 *  @param label_pos label position (offset in source buffer) in source buffer
 */

void link_label(assembly_code* const __restrict dst_code,
                label_table* const   __restrict table,
                const int                       search_indx,
                const int                       label_pos)
        __attribute__((nonnull(1,2)));

/** @brief translate relative/non-relative jump and call instruction
 *  @param dst_code buffer, where translated code will be injected
 *  @param jmp_pos jmp or call position in destination buffer.  When processing labels, it may be
 *  that jmp (call, conditional jmp) comes first and then the label. In this case, jmp (call) is translated
 *  in advance, and when the label is reached, this function is called, and the already
 *  translated jmp (call) receives the address of this label.
 */

void translate_rel32_label(assembly_code* const __restrict dst_code,
                           const size_t                    jmp_pos)
        __attribute__((nonnull(1)));


/** @brief translate conditional and non-conditional jump and call.
 *  @param dst_code buffer, where translated code will be injected
 *  @param jmp_n_call_code a number indicating whether the instruction is conditional or non-conditional jump
 *  or call
 *  @return opcode structure, that contain translated code and size of this chunk of code
 */
opcode translate_jmp_n_call(assembly_code* const __restrict dst_code,
                            const int                       jmp_n_call_code)
       __attribute__((nonnull(1), always_inline));


/** @brief start execution of translated code
 *  @param execution_buffer buffer, whre translated code is stored
 *  @param time_flag flag that flag indicating whether to measure the execution time
 */
void execute_start(char* const __restrict execution_buffer,
                   const int              time_flag)
       __attribute__((nonnull(1)));

/** @brief main function that start translation
 *  @param src_file_name name of source file to translate
 *  @param dst_code buffer, where translated code will be injected
 *  @param time_flag flag that flag indicating whether to measure the translation time
 */

void translation_start(const char *const __restrict    src_file_name,
                       assembly_code *const __restrict dst_buffer,
                       const int                       time_flag)
       __attribute__((nonnull(1,2)));

/** @brief load source code to buffer
 *  @param src_file_name name of source file to translate
 *  @param src_code_save object of assembly_code structure, where the source code will be saved
 *  @return FILE_OPENING_ERROR - if file opening error happened. MALLOC_ERROR - if memory allocation
 *  error happened, else return 0. For more info see TRANSLATION_ERROR.
 *  @see TRANSLATION_ERROR
 */

int load_code(const char    *const __restrict    src_file_name,
              assembly_code *const __restrict src_code_save)
        __attribute__((nonnull(1,2)));

/** @brief handle command line parameters
 *  @param argc command line argument count
 *  @param argv array of strings with this arguments
 */

void command_line_handler(int argc, char* argv[])
        __attribute__((nonnull(2)));

/** @brief simple function, that I declared to simplify translating stdout function in my assembler
 *  language
 *  @param pointer to value, that will be printed
 *  @return as a standart printf
 */
extern "C" int double_printf(double* value);

/** @brief simple function, same as double_printf, but for stdin
 *  @param pointer to value, where the inputted value will be stored
 *  @return as a standart scanf
 */
extern "C" int double_scanf (double* value);


//+======================| INLINE FUNCTIONS DECLARATIONS |===========================+


/** @brief main function that write translated instructions in destination buffer
 *  @param dst_code buffer, where translated code will be written
 *  @param operation_code object of opcode structure. Define the command, that will be written
 */
inline void write_command(assembly_code* const __restrict  dst_code,
                          opcode                           operation_code)
        __attribute__((always_inline, nonnull(1)));


/** @brief translate the instruction "mov rsp, r15". Here in r15 the previous value of rsp is saved
 *  @param dst_code buffer, where translated code will be written
 */

inline void translate_load_rsp(assembly_code* const __restrict dst_code)
        __attribute__((always_inline, nonnull(1)));


/** @brief translate my assembler register individual number to x86 register encoding with this
 *  expression (XMM(1-4) << offset) | suffix;
 *  @param host_reg_id my assembler register individual number
 *  @param suffix suffix needed in above expression
 *  @param offset offset needed in above expression
 *  @return x86 register encoding
 */
inline u_int64_t cvt_host_reg_id_to_native(const int       host_reg_id,
                                           const u_int64_t suffix,
                                           const u_int64_t offset)
       __attribute__((always_inline));

/** @brief translate "push x", where x - double value to x86 instruction 
 *  @param dst_code buffer, where translated code will be written
 *  @param data converted to u_int64_t double value  
 */

inline void translate_push(assembly_code* const __restrict dst_code,
                           const u_int64_t                 data)
        __attribute__((nonnull(1), always_inline));

/** @brief translate "push xx", where xx is ax, bx, cx or dx register in my assembler language
 *  @param dst_code buffer, where translated code will be written
 *  @param reg_id my assembler language register indeficator
 */
inline void translate_push_r(assembly_code* const __restrict dst_code,
                             const int                       reg_id)
        __attribute__((nonnull(1), always_inline));

/** @brief translate cycled jump or call (label is before jump or call)
 *  @param dst_code buffer, where translated code will be written
 *  @param table label table structure. Need to find labels position by knowing jump position
 *  (in this case it is posible because the label is before jump)
 *  @param label_pos in source assembler code. This value will be used to find the appropriate
 *  to label to current jump or call.
 *  @param jmp_n_call_pos position of jump or call in source assembler code. This value also used
 *  to find the appropriate to this jump label
 *  @param jmp_n_call_code indefify which instruction translate (unconditional jump, conditional jump,
 *  call)
 */

inline void translate_cycle(assembly_code* const __restrict dst_code,
                            label_table* const __restrict   table,
                            const int                       label_pos,
                            const int                       jmp_n_call_pos,
                            const int                       jmp_n_call_code)
        __attribute__((nonnull(1,2), always_inline));

/** @brief translate jump or call that is before label. The relative address of label is setted as 0.
 *  @param dst_code buffer, where translated code will be written
 *  @param table label_table is used here to save current jump with appropriate label
 *  @param label_pos label position in source code. This value is used to search in label_table
 *  to "link" current jump with his label
 *  @param jmp_n_call_pos jump or call position in source code. This value is also used for search
 *  @param jmp_n_call_code indefify which instruction translate (unconditional jump, conditional jump,
 *  call)
 */

inline void translate_ahead_jmp_n_call(assembly_code* const __restrict dst_code,
                                       label_table*   const __restrict table,
                                       const int                       label_pos,
                                       const int                       jmp_n_call_pos,
                                       const int                       jmp_n_call_code)
        __attribute__((nonnull(1,2), always_inline));

/** @brief translate jump or call. This function use translate_ahead_jmp_n_call(), translate_push(),
 *  and translate_rel32_label() functions.
 *  @param dst_code buffer, where translated code will be written
 *  @param table label_table is used here to save current jump with appropriate label
 *  @param label_pos label position in source code. This value is used to search in label_table
 *  to "link" current jump with his label.
 *  @param jmp_n_call_pos jump or call position in source code. This value is also used for search
 *  @param jmp_n_call_code indefify which instruction translate (unconditional jump, conditional jump,
  */

inline void jmp_n_call_handler(assembly_code* const __restrict dst_code,
                               label_table*   const __restrict table,
                               const int                       label_pos,
                               const int                       jmp_n_call_pos,
                               const int                       jmp_n_call_code)
        __attribute__((nonnull(1,2), always_inline));

/** @brief translate "mov r15, rsp" in the begining of the buffer to save return address
 *  @param dst_code buffer, where translated code will be written
 */

inline void translate_save_rsp(assembly_code* const __restrict dst_code)
        __attribute__((nonnull(1), always_inline));

/** @brief translate "out" instruction in my assembler language. Out just print the top value in the stack.
 *  @param dst_code buffer, where translated code will be written
 */

inline void translate_stdout(assembly_code* const __restrict dst_code)
       __attribute__((nonnull(1), always_inline));

/** @brief translate two sequential pop for comparison for conditional jump
 *  @param dst_code buffer, where translated code will be written
 *  @param jmp_code indefify which instruction translate (unconditional jump, conditional jump,
 */

inline void translate_two_pop_for_cmp(assembly_code* const __restrict dst_code,
                                      const int                       jmp_code)
       __attribute__((nonnull(1), always_inline));


/** @brief translate "ret" instruction to leave buffer execution
 *  @param dst_code buffer, where translated code will be written
 */

inline void translate_ret(assembly_code* const __restrict dst_code)
        __attribute__((nonnull(1), always_inline));


/** @brief translate arithmetic operations like add, sub, mul, div.
 *  @param dst_code buffer, where translated code will be written
 *  @param op_id indeficator of which operator translate (sub, add, mul or div)
 */

inline void translate_arithmetic_op(assembly_code* const __restrict dst_code,
                                    const int                       op_id)
        __attribute__((nonnull(1), always_inline));
        
//-===========================================================================-


