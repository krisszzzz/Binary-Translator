#if !defined LABEL_TABLE_INCLUDED

#include <cstdint>
#include <stdlib.h>
#include <immintrin.h>
#define LABEL_TABLE_INCLUDED

#define HT_ERROR -1
#define NOT_FOUND -1

#define CYCLE 1
// 128 * 16 * 32 * 16
// TODO: Rework for dynamic value of size
#define MIN_STACK_SIZE       16
#define MIN_LABEL_TABLE_SIZE 128

#define STACK self->elems


struct stack_node
{
        int    label; // 
        int    jmp;
        size_t code_pos;
};
        
struct stack
{
        int            size;
        int            capacity;
        stack_node*    data;
};

struct label_table // Hash table for immediate value
{
        int     size;
        stack*  elems;
};


//+====================| STACK |============================+


void stack_init(stack* const __restrict stack, const size_t size)
       __attribute__((nonnull(1)));


int stack_push(stack* const __restrict stack,
               const int               key,
               const int               data)
        __attribute__((nonnull(1)));

int stack_destr(stack* const __restrict stack)
        __attribute__((nonnull(1)));
//-=========================================================-


int label_table_init(label_table* const __restrict self)
       __attribute__((nonnull(1)));

void label_table_manual_destr(label_table* const __restrict self)
       __attribute__((nonnull(1)));


void label_table_add(label_table* const __restrict  self,
                     const int                      key,
                     const int                      data)
        __attribute__((always_inline, nonnull(1)));


void set_all_cycles(label_table* self,
                    const int    indx,
                    const size_t code_pos,
                    const int    label_pos)
       __attribute__((nonnull(1)));
        
inline int label_table_search_by_label(label_table* const __restrict self,
                                       const int                     label_pos)
      __attribute__((always_inline, hot));


size_t get_code_pos_by_jmp(label_table* const __restrict self,
                           const int                     label_pos,
                           const int                     jmp_pos)
       __attribute__((hot, nonnull(1)));

size_t* get_code_pos_ptr_by_jmp(label_table* const __restrict self,
                                const int                    label_pos,
                                const int                    jmp_pos)
       __attribute__((hot, nonnull(1)));

void label_table__destr(label_table* const __restrict self)
        __attribute__((nonnull(1)));

inline void label_table_add(label_table* const __restrict  self,
                            const int                      label,
                            const int                      jmp)
{       
        int indx = _mm_crc32_u32(label, 0xDED) % MIN_LABEL_TABLE_SIZE;
        //        printf("indx =%d\n", indx);
        stack_push(&self->elems[indx], label, jmp);
}

inline int label_table_search_by_label(label_table* const __restrict self,
                                       const int                     label_pos)
{
        int indx = _mm_crc32_u32(label_pos, 0xDED) % MIN_LABEL_TABLE_SIZE;

        if (STACK[indx].size == 0) {
                return NOT_FOUND;
        }

        return indx;
        
};




#endif
