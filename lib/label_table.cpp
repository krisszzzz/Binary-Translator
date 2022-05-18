#include <label_table.h>
#include <log.h>

#define CODE_POS(count)   STACK[indx].data[count].code_pos
#define JMP_POS(count)    STACK[indx].data[count].jmp
#

void stack_init(stack* const __restrict stack, const size_t size)
{
        stack->data     = (stack_node*)calloc(size,
                                              sizeof(stack_node));
        stack->size     = 0;
        stack->capacity = MIN_STACK_SIZE;
}

int stack_push(stack* const __restrict stack,
               const int               label_pos,
               const int               jmp_pos)
{
        if (stack->size >= stack->capacity) {
                stack->data = (stack_node*)realloc(stack->data,
                                                   stack->capacity * 2 *
                                                   sizeof(stack_node));
                if (stack->data == nullptr) {
                        PrettyPrint("stack->data == nullptr\n");
                        return HT_ERROR;
                }
                
                stack->capacity *= 2;
        }
        
        stack->data[stack->size].label = label_pos;
        stack->data[stack->size++].jmp = jmp_pos;

        return 0;
}

int stack_destr(stack* const __restrict stack)
{
        free(stack->data);
        stack->data = nullptr;
        stack->size = stack->capacity = 0;
        return 0;
}

int label_table_init(label_table* const __restrict self)
{
        self->elems = (stack*)calloc(MIN_LABEL_TABLE_SIZE, sizeof(stack));
        
        for (int iter_count = 0; iter_count < MIN_LABEL_TABLE_SIZE; ++iter_count) {
                stack_init(&self->elems[iter_count], MIN_STACK_SIZE);
        }

        return 0;
}


void label_table_manual_destr(label_table* const __restrict self)
{
        for (int iter_count = 0; iter_count < MIN_LABEL_TABLE_SIZE; ++iter_count) {
                stack_destr(&self->elems[iter_count]);
        }
        
        self->elems = nullptr;
        self->size  = 0;
}

size_t get_code_pos_by_jmp(label_table* const __restrict self,
                           const int                     label_pos,
                           const int                     jmp_pos)
{
        int indx = _mm_crc32_u32(label_pos, 0xDED) % MIN_LABEL_TABLE_SIZE;
        for (int iter_count = 0; iter_count < STACK[indx].size; ++iter_count) {
                if (jmp_pos == JMP_POS(iter_count)) {
                        return CODE_POS(iter_count);
                }
        }

        return (size_t)(NOT_FOUND);
}

size_t* get_code_pos_ptr_by_jmp(label_table* const __restrict self,
                                const int                    label_pos,
                                const int                    jmp_pos)
{
        int indx = _mm_crc32_u32(label_pos, 0xDED) % MIN_LABEL_TABLE_SIZE;
        for (int iter_count = 0; iter_count < STACK[indx].size; ++iter_count) {
                if (jmp_pos == JMP_POS(iter_count)) {
                        return &CODE_POS(iter_count);
                }
        }
        return nullptr;

}

