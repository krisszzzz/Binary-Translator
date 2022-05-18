#include "translator.h"
#include <chrono>

#define MAX_LABEL_LAYER 32

//+===================| SRC AND DST CODE |====================+

int assembly_code_aligned_init(assembly_code* const __restrict self,
                               const size_t                    alignment,
                               const size_t                    bytes)
{
        self->code = (char*)aligned_alloc(alignment, bytes);
         if (self->code == nullptr) {
                PrettyPrint("self->code memory allocation failed\n");
                return MALLOC_ERROR;
        }
        
        self->size     = bytes;
        self->position = 0;
        return 0;

}

int assembly_code_init(assembly_code* const __restrict self,
                       const size_t                    bytes)
{
        self->code = (char*)calloc(bytes, sizeof(char));
        
        if (self->code == nullptr) {
                PrettyPrint("self->code memory allocation failed\n");
                return MALLOC_ERROR;
        }
        
        self->size     = bytes;
        self->position = 0;
        return 0;
}

int load_code(const char*    const __restrict src_file_name,
              assembly_code* const __restrict src_code_save)
{
        FILE* src_file = fopen(src_file_name, "rb");

        if (src_file == nullptr) {
                PrettyPrint("%s: no such file\n",
                            src_file_name);
                return FILE_OPENING_ERROR;
        }

        fseek(src_file, 0, SEEK_END);
        int file_size = ftell(src_file);
        fseek(src_file, 0, SEEK_SET);

        if(assembly_code_init(src_code_save, file_size) == MALLOC_ERROR) {
                return MALLOC_ERROR;
        }
        
        fread(src_code_save->code, sizeof(char), file_size, src_file);
        
        return 0;
        
}

//-===========================================================-

//+=================|STDIN AND STDOUT |=======================+

extern "C" int double_printf(double* value)
{
        // asm(push rbp);
        return printf("%lf\n", *value);
}

extern "C" int double_scanf(double* value)
{
        return scanf("%lf", value);
}

//-===========================================================-


//+==============| CREATING LABEL TABEL |=====================+

void make_label_table(assembly_code* const __restrict src_code,
                      label_table*   const __restrict table)
{
        size_t op_count = src_code->size / sizeof(int);
        int* code = (int*)src_code->code;
        for (size_t iter_count = 0; iter_count < op_count; ++iter_count) {
                switch (code[iter_count]) {
                case PUSH:
                        iter_count += 2;
                        break;
                case CALL:
                case JB:
                case JE:
                case JA:
                case JMP:
                        label_table_add(table, code[iter_count + 1], iter_count);
                        iter_count++;
                        // Thinking...
                        break;
                case PUSHR:
                case POPR:
                case PUSHM:
                case POPM:
                        iter_count++;
                        break;
                        
                case RET:
                case OUT:
                case POP:
                case ADD:
                case DIV:
                case MUL:
                case SUB:
                        break; // Nothing to do (will be optimized out by compiler)
                }
        }
                
}

#define CODE_POS(count)   table->elems[indx].data[count].code_pos
#define JMP_POS(count)    table->elems[indx].data[count].jmp
#define LABEL_POS(count)  table->elems[indx].data[count].label

// For getting info about jmp type
#define CHECK_JMP         (current_code & 0xFF)

void label_setting(assembly_code* const __restrict dst_code,
                   label_table* const   __restrict table,
                   const int                       indx,
                   const size_t                    code_pos,
                   const int                       label_pos)

{
        for (int iter_count = 0; iter_count < table->elems[indx].size; ++iter_count) {
                if (label_pos == LABEL_POS(iter_count)) {
                        if(JMP_POS(iter_count) >= LABEL_POS(iter_count)) {
                                CODE_POS(iter_count) = code_pos;
                        } else {
                                save_jmp_n_call_rel32(dst_code, CODE_POS(iter_count));
                        }
                        
                }
        }
}



//-============================================================-

inline void align_stack(assembly_code* const __restrict dst_code)
{
        constexpr opcode alignment = {
                .code = SUB_RSP_IMM | QWORD << BYTE(3),
                .size = OPSIZE(SUB_RSP_IMM)
        };
        write_command(dst_code, alignment);
}

//+==============| JMP AND CALL HANDLING |=====================+

int save_jmp_n_call_rel32(assembly_code* const __restrict dst_code,
                          const size_t                    code_pos)
{
        char*  temp_code = dst_code->code;
        size_t to_sub    = dst_code->position - code_pos;
        dst_code->code  -= to_sub; 

        const u_int64_t current_code = *(uint_fast64_t*)dst_code->code;
        cvt_u_int64_t_int convert        = {};
        opcode write_rel32               = {};
        
        
       if (JMP_REL32 == CHECK_JMP || NATIVE_CALL == CHECK_JMP) {
                convert.rel_addr = to_sub - OPSIZE(JMP_REL32);
                write_rel32.code = current_code | convert.extended_address << BYTE(1);
                write_rel32.size = sizeof(u_int64_t);
                
        } else {
                convert.rel_addr = to_sub - OPSIZE(JE_REL32);
                write_rel32.code = current_code | convert.extended_address << BYTE(2);
                write_rel32.size = sizeof(u_int64_t);
        }
        
        // We give a relative number to jump, but to_sub is a value that
        // include jmp itself. To correct this, we should sub OPSIZE(JMP_REL32)
        
        write_command(dst_code, write_rel32);
        dst_code->code     = temp_code;
        dst_code->position = code_pos + to_sub; // restore previous value
        return 0;
}

#define GEN_JMP(jmp_type, cmp_type)                                            \
vcmppd.code = VCMPPD_XMM5_XMM0_XMM5 | jmp_type << BYTE(4);                     \
vcmppd.size = OPSIZE(VCMPPD_XMM5_XMM0_XMM5);                                   \
write_command(dst_code, vcmppd);                                               \
write_command(dst_code, movmsk);                                               \
 constexpr opcode cmp  {                                                       \
   .code = cmp_type,                                                           \
   .size = OPSIZE(cmp_type)                                                    \
        };                                                                     \
write_command(dst_code, cmp);                                                  \
jmp.code = JE_REL32;                                                           \
jmp.size = OPSIZE(JE_REL32);



opcode translate_jmp_n_call(assembly_code* const __restrict dst_code,
                            const int                       jmp_n_call_code)
{
        constexpr opcode movmsk = {
                .code = MOVMSKPD_R14D_XMM5,
                .size = OPSIZE(MOVMSKPD_R14D_XMM5)
        };
 
        opcode vcmppd = {};
        opcode jmp    = {};
        
        switch (jmp_n_call_code) {
                // Note that I used brackets because I need a new local area
        case JB: {
                GEN_JMP(LESS, CMP_R14D_1);
                break;
        }
        case JA: {
                GEN_JMP(GREATER, CMP_R14D_1);
                break;
        }
        case JE: {
                GEN_JMP(EQUAL, CMP_R14D_3);
                break;
        }
        case CALL:
                translate_push(dst_code,
                               (u_int64_t)dst_code->code + TRANSLATE_PUSH_SIZE);
                __attribute__((fallthrough));
                               
        case JMP: {
                jmp.code = JMP_REL32;
                jmp.size = OPSIZE(JMP_REL32);
                break;
        }
        }
                
        return jmp;

}


inline void translate_two_pop_for_cmp(assembly_code* const __restrict dst_code,
                                      const int                       jmp_n_call_code)
{
        if (jmp_n_call_code != JMP) {
                constexpr opcode get_first_elem = {
                        .code =  VMOVQ_XMM_RSP_IMM | XMM0_EXTEND << BYTE(3) | XMMWORD << BYTE(5),
                        .size = OPSIZE(VMOVQ_XMM_RSP_IMM)
                };
                
                constexpr opcode get_second_elem = {
                        .code =  VMOVQ_XMM_RSP_IMM | XMM5_EXTEND << BYTE(3) | 2*XMMWORD << BYTE(5),
                        .size = OPSIZE(VMOVQ_XMM_RSP_IMM)
                };

                constexpr opcode add_rsp = {
                        .code = ADD_RSP_IMM | 2*XMMWORD << BYTE(3),
                        .size = OPSIZE(ADD_RSP_IMM)
                };

                write_command(dst_code, get_first_elem);
                write_command(dst_code, get_second_elem);
                write_command(dst_code, add_rsp);
        }


}

inline void translate_ahead_jmp_n_call(assembly_code* const __restrict dst_code,
                                       label_table*   const __restrict table,
                                       const int                       label_pos,
                                       const int                       jmp_n_call_pos,
                                       const int                       jmp_n_call_code)
{
        translate_two_pop_for_cmp(dst_code, jmp_n_call_code);
       
        opcode jmp = translate_jmp_n_call(dst_code, jmp_n_call_code);

        size_t* code_pos_ptr = get_code_pos_ptr_by_jmp(table, label_pos, jmp_n_call_pos);
        *code_pos_ptr        = dst_code->position;

        write_command(dst_code, jmp);

}

inline void jmp_n_call_handler(assembly_code* const __restrict dst_code,
                               label_table*   const __restrict table,
                               const int                       label_pos,
                               const int                       jmp_n_call_pos,
                               const int                       jmp_n_call_code)
{
        // For case when label is before jump
        if (label_pos <= jmp_n_call_pos) {
                translate_cycle(dst_code,
                                table,
                                label_pos,
                                jmp_n_call_pos,
                                jmp_n_call_code);
        } else {
                translate_ahead_jmp_n_call(dst_code,
                                           table,
                                           label_pos,
                                           jmp_n_call_pos,
                                           jmp_n_call_code);
        }
                        // For case label after jump

 
}

inline void translate_cycle(assembly_code* const __restrict dst_code,
                            label_table* const __restrict   table,
                            const int                       label_pos,
                            const int                       jmp_n_call_pos,
                            const int                       jmp_n_call_code)
                    
{
        
        translate_two_pop_for_cmp(dst_code, jmp_n_call_code);
        
        size_t code_pos =  get_code_pos_by_jmp(table,
                                               label_pos,
                                               jmp_n_call_pos);

        cvt_u_int64_t_int convert = {};

        opcode jmp = translate_jmp_n_call(dst_code, jmp_n_call_code);

        // Call is treated as a jump with saving the return address in the stack
        switch(jmp_n_call_code) {
        case CALL:
                translate_push(dst_code,
                               (u_int64_t)dst_code->code + TRANSLATE_PUSH_SIZE);
                __attribute__((fallthrough));
                
        case JMP:
                // Save rel address
                convert = { .rel_addr =
                            code_pos - dst_code->position - OPSIZE(JMP_REL32)};
                jmp.code |= convert.extended_address << BYTE(1);
                break;
       default:
                convert = { .rel_addr = code_pos - dst_code->position - OPSIZE(JE_REL32)};
                jmp.code |= convert.extended_address << BYTE(2);
        }
                
        write_command(dst_code, jmp);
}

//-============================================================-


//+=================| WRITTING COMMANDS |======================+
        
inline void write_command(assembly_code* const __restrict dst_code,
                          opcode                          operation_code)
{
        *(u_int64_t*)dst_code->code = operation_code.code; 
        dst_code->position         += operation_code.size;
        dst_code->code             += operation_code.size;
}

//-============================================================-
        

//+===========| CONVERT HOST TO NATIVE REGISTER  |=============+
inline u_int64_t cvt_host_reg_id_to_native(const int       host_reg_id,
                                           const u_int64_t suffix,
                                           const u_int64_t offset)
{
        
        switch(host_reg_id) {
        case AX:
                return (XMM1 << offset) | suffix;
        case BX:
                return (XMM2 << offset) | suffix;
        case CX:
                return (XMM3 << offset) | suffix;
        case DX:
                return (XMM4 << offset) | suffix;
        default:
                PrettyPrint("Unknown host register id = %d\n", host_reg_id);
                return CVT_ERROR;
        }
}


//-============================================================-

//+==================| PUSH TRANSLATION |======================+

inline void translate_push_r(assembly_code* const __restrict dst_code,
                             const int                       reg_id)
{
        
        constexpr u_int64_t offset = 3;  // to binary move, to make correct mask
        constexpr u_int64_t suffix = 4;  // (4 = 00000100b)
        

        // Convetation to native register op code, using mask.
        u_int64_t reg_op_code = cvt_host_reg_id_to_native(reg_id, suffix, offset);

        opcode vmovq_code = {
                .code = (VMOVQ_RSP_XMM) | (reg_op_code << BYTE(3)),
                .size = OPSIZE(VMOVQ_RSP_XMM)
        };
        
        write_command(dst_code, vmovq_code);

        constexpr opcode rsp_sub_code = { 
                .code = SUB_RSP_IMM | (XMMWORD << BYTE(3)), 
                .size = OPSIZE(SUB_RSP_IMM)
        };
        
        write_command(dst_code, rsp_sub_code);
}

inline void translate_push_m(assembly_code* const __restrict dst_code,
                             const u_int64_t                 memory_indx) 
{
        opcode get_mem           = {}; 

        // One byte memory index
        if (memory_indx <= 127) {
                get_mem.code = VMOVQ_XMM5_R13_B_IMM | memory_indx << BYTE(5);
                get_mem.size = OPSIZE(VMOVQ_XMM5_R13_B_IMM);
                write_command(dst_code, get_mem);
        } else {
                opcode long_index = {
                        .code = memory_indx,
                        .size = DWORD
                };
                
                get_mem.code = VMOVQ_XMM5_R13_D_IMM;
                get_mem.size = OPSIZE(VMOVQ_XMM5_R13_D_IMM);
                
                write_command(dst_code, get_mem);
                write_command(dst_code, long_index);
        }

        
        constexpr opcode mov_to_stack {
                .code = VMOVQ_RSP_XMM | XMM5_BASE << BYTE(3),
                .size = OPSIZE(VMOVQ_RSP_XMM)
        };
        
        constexpr opcode sub_rsp = {
                .code = SUB_RSP_IMM | XMMWORD << BYTE(3),
                .size = OPSIZE(SUB_RSP_IMM)
        };

        
        write_command(dst_code, mov_to_stack);
        write_command(dst_code, sub_rsp);
        
}

inline void translate_push(assembly_code* const __restrict dst_code,
                           const u_int64_t                 data)
{
        constexpr opcode mov_r14_imm = {
                .code = MOV_R14,
                .size = OPSIZE(MOV_R14)
        };

        constexpr u_int64_t double_size = sizeof(double); // 

        opcode write_data  = {
                .code = data,
                .size = double_size
        };

        constexpr opcode save_to_stack {
                .code = MOV_TO_STACK_R14,
                .size = OPSIZE(MOV_TO_STACK_R14)
        };

        constexpr opcode sub_rsp {
                .code = SUB_RSP_IMM | XMMWORD << BYTE(3),
                .size = OPSIZE(SUB_RSP_IMM)
        };

        write_command(dst_code, mov_r14_imm);
        write_command(dst_code, write_data);
        write_command(dst_code, save_to_stack);
        write_command(dst_code, sub_rsp);
}

//-============================================================-


//+==================| POP TRANSLATION |=======================+

inline void translate_pop_m(assembly_code* const __restrict dst_code,
                            const u_int64_t                 memory_indx)
{
        constexpr opcode add_rsp = {
                .code = ADD_RSP_IMM | XMMWORD << BYTE(3),
                .size = OPSIZE(ADD_RSP_IMM)
        };
        
        
        constexpr opcode get_top = {
                .code = VMOVQ_XMM_RSP_IMM | XMM5_EXTEND << BYTE(3)
                        | XMMWORD << BYTE(5),
                .size = OPSIZE(VMOVQ_XMM_RSP_IMM)
        };


        write_command(dst_code, get_top);
        
        opcode pop_mem = {};

        if (memory_indx <= 127) {
                pop_mem.code = VMOVQ_R13_B_IMM_XMM5 | memory_indx << BYTE(5);
                pop_mem.size = OPSIZE(VMOVQ_R13_B_IMM_XMM5);
                write_command(dst_code, pop_mem);
        } else {

                opcode memory_long_indx = {
                        .code = memory_indx,
                        .size = DWORD
                };
                
                pop_mem.code = VMOVQ_R13_D_IMM_XMM5;
                pop_mem.size = OPSIZE(VMOVQ_R13_D_IMM_XMM5);

                write_command(dst_code, pop_mem);
                write_command(dst_code, memory_long_indx);

        }
 

        write_command(dst_code, add_rsp);
        
        
}

inline void translate_pop(assembly_code* const __restrict dst_code)
{

        constexpr opcode rsp_sub_code = { 
                .code = ADD_RSP_IMM | (XMMWORD << BYTE(3)), 
                .size = OPSIZE(ADD_RSP_IMM)
        };
        
        write_command(dst_code, rsp_sub_code);
}

inline void translate_pop_r(assembly_code* const __restrict dst_code,
                            const int                       reg_id)
{
        constexpr u_int64_t suffix           = 0x44; // 0x44 = 01000100b watch explanation of opcodes higher
        constexpr u_int64_t offset           = 3;    //  

        // Convetation to native register op code, using mask.
        u_int64_t reg_op_code = cvt_host_reg_id_to_native(reg_id, suffix, offset);

        opcode movq_xmm_rsp = {
                .code = VMOVQ_XMM_RSP_IMM | (reg_op_code << BYTE(3)) |
                        (XMMWORD << BYTE(5)),
                .size = OPSIZE(VMOVQ_XMM_RSP_IMM)
                
        };

        write_command(dst_code, movq_xmm_rsp);
        
        constexpr opcode rsp_sub_code = { 
                .code = ADD_RSP_IMM | (XMMWORD << BYTE(3)), 
                .size = OPSIZE(ADD_RSP_IMM)
        };
        
        write_command(dst_code, rsp_sub_code);


};

//-============================================================-


//+=====================| SET DATA |===========================+

inline void set_data_segment(assembly_code* const __restrict dst_code)
{
        // Note: for correct work should be allocated 993 * 8 = 7994
        // But for correct work of mprotect you should align your buffer by PAGE_SIZE bytes
        
        const u_int64_t data_address = (u_int64_t)(dst_code->code -
                                                   2 * PAGESIZE);
        
        constexpr opcode mov_r14_imm = {
                .code = MOV_R13,
                .size = OPSIZE(MOV_R13)
        };

        opcode write_data  = {
                .code = data_address,
                .size = sizeof(u_int64_t)
        };
        
        write_command(dst_code, mov_r14_imm);
        write_command(dst_code, write_data);
        
}


//-============================================================-


//+=============| STDIN AND OUT TRANSLATION  |=================+

// Save all used registers
#define PUSHA                      \
do {                               \
translate_push_r(dst_code, AX);    \
translate_push_r(dst_code, BX);    \
translate_push_r(dst_code, CX);    \
translate_push_r(dst_code, DX);    \
 }                                 \
 while(0)


// Get back all used registers 
#define POPA                           \
do {                                   \
        translate_pop_r(dst_code, DX); \
        translate_pop_r(dst_code, CX); \
        translate_pop_r(dst_code, BX); \
        translate_pop_r(dst_code, AX); \
 } while(0)
        



inline void translate_stdout(assembly_code* const __restrict dst_code)
{
        opcode lea_rdi_rsp_16 = {
                .code = LEA_RDI_RSP_16,
                .size = OPSIZE(LEA_RDI_RSP_16)
        };


        write_command(dst_code, lea_rdi_rsp_16);
       
        PUSHA;
        
        u_int64_t code_address = (u_int64_t)(dst_code->code + OPSIZE(NATIVE_CALL));
        u_int64_t fnct_address = (u_int64_t)(double_printf);

        int rel_address        = (int)(fnct_address - code_address);

        cvt_u_int64_t_int convert = {.rel_addr = rel_address};


        opcode call_stdout = {
                .code = NATIVE_CALL | convert.extended_address << BYTE(1),
                .size = OPSIZE(NATIVE_CALL)
        };

        write_command(dst_code, call_stdout);
        POPA;
}

inline void translate_stdin(assembly_code* const __restrict dst_code)
{
        constexpr opcode mov_rdi_rsp = {
                .code = MOV_RDI_RSP,
                .size = OPSIZE(MOV_RDI_RSP)
        };

        constexpr opcode sub_rsp_xmmword = {
                .code = SUB_RSP_IMM | XMMWORD << BYTE(3),
                .size = OPSIZE(SUB_RSP_IMM)
        };

        write_command(dst_code, mov_rdi_rsp);
        
        u_int64_t code_address = (u_int64_t)(dst_code->code + OPSIZE(NATIVE_CALL));
        u_int64_t fnct_address = (u_int64_t)(double_scanf);

        int rel_address  = (int)(fnct_address - code_address);

        cvt_u_int64_t_int convert = {.rel_addr = rel_address};


        opcode call_stdin = {
                .code = NATIVE_CALL | convert.extended_address << BYTE(1),
                .size = OPSIZE(NATIVE_CALL)
        };

        write_command(dst_code, call_stdin);
        
        write_command(dst_code, sub_rsp_xmmword);
}

//-============================================================-

//+======================| HLT |===============================+

inline void translate_hlt(assembly_code* const __restrict dst_code)
{
        translate_load_rsp(dst_code);
        *dst_code->code = (char)NATIVE_RET;
        dst_code->code++;
        dst_code->position++;
}


//-============================================================-

//+================| ARITHMETIC OPERATIONS |===================+


inline void translate_arithmetic_op(assembly_code* const __restrict dst_code,
                                    const int                       op_id)
{
        u_int64_t operation_code = 0;
        switch (op_id) {
        case ADD:
                operation_code = ADDSD_XMM_RSP_IMM;
                break;
        case SUB:
                operation_code = SUBSD_XMM_RSP_IMM;
                break;
        case MUL:
                operation_code = MULSD_XMM_RSP_IMM;
                break;
        case DIV:
                operation_code = DIVSD_XMM_RSP_IMM;
                break;
        default:
                PrettyPrint("Unexpected operator with id = %d\n", op_id);
        }

        constexpr opcode get_operand = {
                .code = VMOVQ_XMM_RSP_IMM | (XMM5_EXTEND << BYTE(3)) | (XMMWORD << BYTE(5)),
                .size = OPSIZE(VMOVQ_XMM_RSP_IMM)
        };

        write_command(dst_code, get_operand);

        opcode arithmetic_op = {
                .code =  operation_code | (XMM5_EXTEND << BYTE(3)) | (2*XMMWORD << BYTE(5)),
                .size =  OPSIZE(ADDSD_XMM_RSP_IMM)
        };
                         // Operation has same sizes
        write_command(dst_code, arithmetic_op);
        
        constexpr opcode save_result = {
                .code = VMOVQ_RSP_IMM_XMM | XMM5_EXTEND << BYTE(3) | 2*XMMWORD << BYTE(5),
                .size = OPSIZE(VMOVQ_RSP_IMM_XMM)
        };


        write_command(dst_code, save_result);
        translate_pop(dst_code);
        
}

//-============================================================-

//+=======================| SQRT |=============================+

inline void translate_sqrt(assembly_code* const __restrict dst_code)
{
        constexpr opcode get_top =  {
                .code = VMOVQ_XMM_RSP_IMM | XMM0_EXTEND << BYTE(3) | XMMWORD << BYTE(5),
                .size = OPSIZE(VMOVQ_XMM_RSP_IMM)
        };

        write_command(dst_code, get_top);

        constexpr opcode vsqrt_xmm0 = {
                .code = VSQRTPD_XMM0_XMM0,
                .size = OPSIZE(VSQRTPD_XMM0_XMM0)
        };

        write_command(dst_code, vsqrt_xmm0);

        constexpr opcode save_res = {
                .code = VMOVQ_RSP_IMM_XMM | XMM0_EXTEND << BYTE(3) | XMMWORD << BYTE(5),
                .size = OPSIZE(VMOVQ_RSP_IMM_XMM)
        };

        write_command(dst_code, save_res);
}


//-============================================================-


//+===============| RET ADDRESS SAVING |=======================+

inline void translate_save_rsp(assembly_code* const __restrict dst_code)
{
        constexpr opcode mov_r15_rsp = {
                .code = MOV_R15_RSP,
                .size = OPSIZE(MOV_R15_RSP)
        };
        
        write_command(dst_code, mov_r15_rsp);
        constexpr u_int64_t qword_size = 0x08;

        constexpr opcode rsp_sub_code = {
                .code = SUB_RSP_IMM | (qword_size << BYTE(3)),
                .size = OPSIZE(SUB_RSP_IMM)
        };
        
        write_command(dst_code, rsp_sub_code);
}

inline void translate_load_rsp(assembly_code* const __restrict dst_code)
{
        
        constexpr opcode mov_rsp_rbp = {
                .code = MOV_RSP_R15,
                .size = OPSIZE(MOV_RSP_R15)
        };
        write_command(dst_code, mov_rsp_rbp);
}


inline void translate_ret(assembly_code* const __restrict dst_code)
{
        constexpr opcode add_rsp_alignment_size = {
                .code = ADD_RSP_IMM | XMMWORD << BYTE(3),
                .size = OPSIZE(ADD_RSP_IMM)
        };

        write_command(dst_code, add_rsp_alignment_size);
        
        *dst_code->code = (char)NATIVE_RET;
        dst_code->code++;
        dst_code->position++;
}

//-============================================================-


//+================| EXECUTION START |=========================+

void execute_start(char* const __restrict execution_buffer,
                   const int              time_flag)
{


        auto start = std::chrono::high_resolution_clock::now();
        if (mprotect(execution_buffer, MIN_DST_CODE_SIZE, PROT_EXEC) != 0) {
                PrettyPrint("mprotect error\n");
                return;
        }

        
        void (*execution_address)(void) = (void (*)(void))(execution_buffer);
        // Execution start
        execution_address();

        if (time_flag) {
                auto stop     = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
                                                                      (stop - start);
 
                printf("Execution time: %ld ms\n",
                       duration.count());
        }
}

//-============================================================-


//+======================| DLL |===============================+

#define PUSH_HANDLER                                                    \
        PUSHM:                                                          \
        translate_push_m(dst_buffer, (u_int64_t)code[iter_count + 1]);  \
        iter_count++;                                                   \
        break;                                                          \
        case PUSHR:                                                     \
        translate_push_r(dst_buffer, code[iter_count + 1]);             \
        iter_count++;                                                   \
        break;                                                          \
        case PUSH:                                                      \
        translate_push(dst_buffer,                                      \
                       *(u_int64_t*)&code[iter_count + 1]);             \
        iter_count += 2;                                                \
        break

#define POP_HANDLER                                                     \
        POP:                                                            \
        translate_pop(dst_buffer);                                      \
        break;                                                          \
        case POPR:                                                      \
        translate_pop_r(dst_buffer, code[iter_count + 1]);              \
        iter_count++;                                                   \
        break;                                                          \
        case POPM:                                                      \
        translate_pop_m(dst_buffer, (u_int64_t)code[iter_count + 1]);   \
        iter_count++;                                                   \
        break                                                          

       
#define CALL_N_JUMP_HANDLER                                             \
        CALL:                                                           \
        case JB:                                                        \
        case JE:                                                        \
        case JA:                                                        \
        case JMP:                                                       \
        jmp_n_call_handler(dst_buffer,                                  \
                &table,                                                 \
                code[iter_count + 1],                                   \
                iter_count,                                             \
                code[iter_count]);                                      \
        iter_count++;                                                   \
        break                                                            

#define ARITHMETIC_OPERATIONS                                     \
        ADD:                                                      \
        case DIV:                                                 \
        case MUL:                                                 \ 
        case SUB:                                                 \
        translate_arithmetic_op(dst_buffer, code[iter_count]);    \
        break;                                                    \
        case SQRT:                                                \
        translate_sqrt(dst_buffer);                               \
        break;



#define STDIO_HANDLER                                   \
        IN:                                             \
        translate_stdin(dst_buffer);                    \
        break;                                          \
        case OUT:                                       \
        translate_stdout(dst_buffer);                   \
        break                                          
                        
#define RET_HLT_HANLDER                                 \
        RET:                                            \
        translate_ret(dst_buffer);                      \
        break;                                          \
        case HLT:                                       \
        translate_hlt(dst_buffer);                      \
        break                                          

#define LABEL_SET                                                       \
        search_res = label_table_search_by_label(&table,                \
                                                 iter_count);           \
                                                                        \
        if (search_res != NOT_FOUND) {                                  \
                label_setting(dst_buffer,                               \
                              &table,                                   \
                              search_res,                               \
                              dst_buffer->position,                     \
                              iter_count);                              \
        }


                        
//-============================================================-


//+===================| START TRANSLATION |====================+


int translation_start(const char *const __restrict    src_file_name,
                      assembly_code *const __restrict dst_buffer,
                      const int                       time_flag)
    
{
        auto start = std::chrono::high_resolution_clock::now();

       // Code loading
        assembly_code src_code = {};
        load_code(src_file_name, &src_code); 
        int* code = (int*)src_code.code;
        assembly_code_aligned_init(dst_buffer, PAGESIZE, MIN_DST_CODE_SIZE);
        
        // data allocation
        size_t op_count = src_code.size / sizeof(int);
        dst_buffer->code += 2 * PAGESIZE; // Allocate memory
        set_data_segment(dst_buffer);
        translate_save_rsp(dst_buffer);       

        // label table init
        label_table table = {};
        label_table_init(&table);
        make_label_table(&src_code, &table);

        int search_res = 0;
                
        for (size_t iter_count = 0; iter_count < op_count; ++iter_count) {

                LABEL_SET;
                switch (code[iter_count]) {
                        
                case RET_HLT_HANLDER;
                case CALL_N_JUMP_HANDLER;
                
                case POP_HANDLER;
                case PUSH_HANDLER;
                
                case ARITHMETIC_OPERATIONS;
                
                case STDIO_HANDLER;
                }
                
        }

        translate_load_rsp(dst_buffer);
        *dst_buffer->code = (char)NATIVE_RET;
        // Add ret to the end of buffer
        dst_buffer->code -= dst_buffer->position;
        // Go back to the code begin

        if (time_flag) {

                auto stop     = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
                                                                      (stop - start);
               
                printf("Translation time: %ld ms\n",
                       duration.count());
        }
        
        return 0;
}




//+=========================| EMULATOR EXECUTE |==============================+

#define MAX_STR_SIZE 256
#define CPU_BIN_NAME "./cpu "

void execute_emulator(const char* const __restrict file_name,
                      const int                    time_flag)
{

        auto start = std::chrono::high_resolution_clock::now();
       
        char sys_string[MAX_STR_SIZE] = CPU_BIN_NAME;

        strncpy(sys_string + strlen(CPU_BIN_NAME),
                file_name,
                MAX_STR_SIZE - strlen(CPU_BIN_NAME));

        system(sys_string);

        if (time_flag) {
                auto stop     = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
                                                                      (stop - start);
                        
                printf("Execution time: %ld ms\n",
                        duration.count());
        }
        
}


//+===========================| COMMAND LINE |================================+

#define NON_NATIVE 1
#define TIME       2

#define SET_BIT(position) *option_mask |= (1 << position) 
#define GET_BIT(position) option_mask & (1 << position)

inline void option_detecting(const char* const __restrict option,
                             int* const __restrict        option_mask)
{
        if (strcmp(option, "--non-native") == 0) {
                SET_BIT(NON_NATIVE);
        } else if (strcmp(option, "--time") == 0) {
                SET_BIT(TIME);
        } else {
                printf(BLUE "Warning:" END
                            "Unrecognizable option %s "
                            "was ignored.  Type --help "
                            "to show all options\n", option);
        }
}

void  option_handling(int                          option_mask,
                      const char* const __restrict file_name)
{
        if (GET_BIT(NON_NATIVE)) {
                execute_emulator(file_name, GET_BIT(TIME));
               
        } else {
                assembly_code execute = {};
                translation_start(file_name, &execute, GET_BIT(TIME));
                execute_start(execute.code, GET_BIT(TIME));
        }
                      
        
}

void command_line_handler(int argc, char* argv[])
{
        int option_mask     = 0;
        int file_name_index = 0;
        // Skip last element
        for (int iter_count = 1; iter_count < argc; ++iter_count) {
                if (*argv[iter_count] == '-') {
                        option_detecting(argv[iter_count],
                                         &option_mask);
                } else {
                        if (file_name_index != 0) {
                                printf(RED "Fatal error: " END
                                           "You cannot translate more "
                                           "than one file\n");
                                return;
                        }

                        file_name_index = iter_count;
                }
        }

        if (file_name_index == 0) {
                printf(RED "Fatal error: " END
                           "expected file name\n");
                return;
        }

        option_handling(option_mask, argv[file_name_index]);
}

//-===========================================================================-
