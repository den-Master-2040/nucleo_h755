/*
  FILE...: ldpc_codes.c
  AUTHOR.: David Rowe
  CREATED: July 2020

  Array of LDPC codes used for various Codec2 waveforms.
*/

#include "ldpc_codes.h"

#include <stdio.h>
#include <string.h>

#include "HRA_112_112.h"
#include "HRA_56_56.h"
#include "H_1024_2048_4f.h"
#include "assert.h"
#include "interldpc.h"

struct LDPC ldpc_codes[] = {
{"HRA_112_112", HRA_112_112_MAX_ITER, 0, 1, 1, HRA_112_112_CODELENGTH,
     HRA_112_112_NUMBERPARITYBITS, HRA_112_112_NUMBERROWSHCOLS,
     HRA_112_112_MAX_ROW_WEIGHT, HRA_112_112_MAX_COL_WEIGHT,
     (uint16_t *)HRA_112_112_H_rows, (uint16_t *)HRA_112_112_H_cols},
{"HRA_56_56", HRA_56_56_MAX_ITER, 0, 1, 1, HRA_56_56_CODELENGTH,
     HRA_56_56_NUMBERPARITYBITS, HRA_56_56_NUMBERROWSHCOLS,
     HRA_56_56_MAX_ROW_WEIGHT, HRA_56_56_MAX_COL_WEIGHT,
     (uint16_t *)HRA_56_56_H_rows, (uint16_t *)HRA_56_56_H_cols},
#ifndef __EMBEDDED__

{"H_1024_2048_4f", H_1024_2048_4f_MAX_ITER, 0, 1, 1,
     H_1024_2048_4f_CODELENGTH, H_1024_2048_4f_NUMBERPARITYBITS,
     H_1024_2048_4f_NUMBERROWSHCOLS, H_1024_2048_4f_MAX_ROW_WEIGHT,
     H_1024_2048_4f_MAX_COL_WEIGHT, (uint16_t *)H_1024_2048_4f_H_rows,
     (uint16_t *)H_1024_2048_4f_H_cols}
#endif
};

int ldpc_codes_num(void) { return sizeof(ldpc_codes) / sizeof(struct LDPC); }

void ldpc_codes_list() {
  fprintf(stderr, "\n");
  for (int c = 0; c < ldpc_codes_num(); c++) {
    int n = ldpc_codes[c].NumberRowsHcols + ldpc_codes[c].NumberParityBits;
    int k = ldpc_codes[c].NumberRowsHcols;
    float rate = (float)k / n;
    fprintf(stderr, "%-20s rate %3.2f (%d,%d) \n", ldpc_codes[c].name,
            (double)rate, n, k);
  }
  fprintf(stderr, "\n");
}

int ldpc_codes_find(char name[]) {
  int code_index = -1;
  for (int c = 0; c < ldpc_codes_num(); c++)
    if (strcmp(ldpc_codes[c].name, name) == 0) code_index = c;
  assert(code_index != -1); /* code not found */
  return code_index;
}

void ldpc_codes_setup(struct LDPC *ldpc, char name[]) {
  int code_index;
  code_index = ldpc_codes_find(name);
  assert(code_index != -1);
  memcpy(ldpc, &ldpc_codes[code_index], sizeof(struct LDPC));
  set_up_ldpc_constants(ldpc, ldpc->CodeLength, ldpc->NumberParityBits);
}
