#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct
{
    const char* name;
    int base_opcode;
    int imm_bytes; // 0 1 2 4 8
} InstructionDescriptor;

typedef struct {
    const char* name;
    int index; // 0-7 for RAX-RDI
} RegisterDescriptor;

InstructionDescriptor instructions[] = 
{
    {"MOV", 0xB8, 8},
};

RegisterDescriptor registers[] = 
{
    {"RAX", 0},
    {"RCX", 1},
    {"RDX", 2},
    {"RBX", 3},
    {"RSP", 4},
    {"RBP", 5},
    {"RSI", 6},
    {"RDI", 7},
};

int register_index(const char* name)
{
    int n = sizeof(registers) / sizeof(registers[0]);
    for (int i = 0; i < n; i++)
        if (strcmp(name, registers[i].name) == 0)
            return registers[i].index;
    return -1;
}

InstructionDescriptor* find_instruction(const char* name)
{
    int n = sizeof(instructions) / sizeof(instructions[0]);
    for(int i = 0; i < n; i++)
        if(strcmp(name,instructions[i].name) == 0)
            return &instructions[i];
    return NULL;
}

uint64_t parse_number(const char* s)
{
    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
        return strtoull(s + 2, NULL, 2);

    return strtoull(s, NULL, 0);
}

void assemble(FILE* in, FILE* out)
{
    char line[256];
    while (fgets(line, sizeof(line), in))
    {
        line[strcspn(line, "\n")] = 0;


        for(int i = 0; line[i]; i++)
        {
            line[i] = toupper(line[i]);
        }

        char instr[16], reg[16], imm_str[32];

        if (sscanf(line, "%15s %15[^,], %31s", instr, reg, imm_str) == 3)
        {
            uint64_t imm = parse_number(imm_str);

            InstructionDescriptor* desc = find_instruction(instr);
            if (!desc) { printf("Unknown instruction: %s\n", instr); continue; }
            
            int reg_idx = register_index(reg);
            if (reg_idx < 0) { printf("Unknown register: %s\n", reg); continue; }

            uint8_t rex = 0x48;
            uint8_t opcode = desc->base_opcode + reg_idx;
            
            fwrite(&rex, 1, 1, out);
            fwrite(&opcode, 1, 1, out);
            fwrite(&imm, desc->imm_bytes, 1, out);
        }
        else
        {
            printf("Invalid line: %s\n", line);
        }
    }
}

int main()
{
    FILE* in = fopen("tinyasm.asm","r");
    FILE* out = fopen("tinybin.bin","wb");

    if (!in || !out) { printf("Error opening file\n"); return 1; }

    assemble(in, out);

    fclose(in);
    fclose(out);
    return 0;
}
