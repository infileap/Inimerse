/* docs/AUDIT.md 1.54 -- vm_pool_slot cannot refuse an index.
 *
 * The function is the single door to the array/dict pool: 71 call sites in
 * src/ go through it.  Its body had no bounds test at all --
 *
 *     if (idx >= 0 && idx < 4096) return &vm->arrays[idx];
 *     return &vm->arrays_big[idx - 4096];
 *
 * -- so it could never answer NULL, and eight call sites nonetheless wrote
 * `if (!a) return 0;` after it, as if it could.  A negative index returned
 * `vm->arrays_big - 4097`, which on a fresh VM (arrays_big == NULL, bigCap == 0)
 * is a wild low address; an index past bigCap returned a slot beyond the
 * allocation.  src/vm/vm.c:2818 already uses the right predicate for the same
 * handle -- `ival > 0 && ival - 1 < vm->arrayCount` -- so the check existed in
 * one place and was missing in the one function everything else calls.
 *
 * The local names below are `deep` and `edge`, not `far` and `at`: mingw
 * defines `far` (legacy `__far`) as an empty macro in its system headers, so
 * `ArrayObj *far = ...` fails to compile on Windows with "expected identifier
 * or '(' before '=' token".  Found by running -fsyntax-only with mingw64; the
 * Linux build could not see it.
 *
 * This probe asserts the refusal, and also that the two indices that ARE valid
 * still resolve, so a "return NULL" that broke the pool cannot pass it.
 *
 * It does not dereference anything: an out-of-range answer is a pointer value,
 * and on the pre-fix build that value is the evidence.
 */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    VM *vm = malloc(sizeof(VM));
    if (!vm) { printf("vm_pool_slot_probe: cannot allocate a VM\n"); return 1; }
    memset(vm, 0, sizeof(VM));   /* a fresh VM: arrays_big == NULL, bigCap == 0 */

    int bad = 0;

    /* The two ends of the inline pool must still resolve. */
    if (vm_pool_slot(vm, 0) != &vm->arrays[0]) {
        printf("FAIL slot(0) did not answer &arrays[0]\n"); bad++;
    }
    if (vm_pool_slot(vm, 4095) != &vm->arrays[4095]) {
        printf("FAIL slot(4095) did not answer &arrays[4095]\n"); bad++;
    }

    /* Everything outside it must be refused, not answered. */
    ArrayObj *neg  = vm_pool_slot(vm, -1);
    ArrayObj *deep  = vm_pool_slot(vm, -4097);
    ArrayObj *edge   = vm_pool_slot(vm, 4096);
    ArrayObj *huge = vm_pool_slot(vm, 100000);

    printf("vm_pool_slot_probe: raw -1=%p -4097=%p 4096=%p 100000=%p\n",
           (void *)neg, (void *)deep, (void *)edge, (void *)huge);

    if (neg)  { printf("FAIL slot(-1) answered %p, want NULL\n", (void *)neg); bad++; }
    if (deep)  { printf("FAIL slot(-4097) answered %p, want NULL\n", (void *)deep); bad++; }
    if (edge)   { printf("FAIL slot(4096) answered %p with bigCap==0, want NULL\n", (void *)edge); bad++; }
    if (huge) { printf("FAIL slot(100000) answered %p, want NULL\n", (void *)huge); bad++; }

    /* A grown pool: the slots inside it resolve, the first one past it does not. */
    vm->arrays_big = calloc(4, sizeof(ArrayObj));
    if (!vm->arrays_big) { printf("FAIL cannot allocate arrays_big\n"); return 1; }
    vm->bigCap = 4;

    if (vm_pool_slot(vm, 4096) != &vm->arrays_big[0]) {
        printf("FAIL slot(4096) did not answer &arrays_big[0]\n"); bad++;
    }
    if (vm_pool_slot(vm, 4099) != &vm->arrays_big[3]) {
        printf("FAIL slot(4099) did not answer &arrays_big[3]\n"); bad++;
    }
    ArrayObj *past = vm_pool_slot(vm, 4100);
    if (past) {
        printf("FAIL slot(4100) answered %p with bigCap==4, want NULL\n", (void *)past); bad++;
    }
    /* and the negative case is still refused once arrays_big is non-NULL, which is
     * the one the pre-fix body turned into a real address into the heap. */
    ArrayObj *neg2 = vm_pool_slot(vm, -1);
    if (neg2) {
        printf("FAIL slot(-1) answered %p with arrays_big set, want NULL\n", (void *)neg2); bad++;
    }

    free(vm->arrays_big);
    free(vm);

    if (bad) { printf("vm_pool_slot_probe: %d failure(s)\n", bad); return 1; }
    printf("vm_pool_slot_probe: ok\n");
    return 0;
}
