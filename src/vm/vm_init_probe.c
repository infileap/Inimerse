/* Pins one contract: vm_init leaves no field indeterminate.
 *
 * The defect this pins: vm_init was a field-by-field list with no memset, and
 * prof_enabled / prof_state were never in it. A stack VM (src/main.c:949 and
 * :979 both do `VM vm; vm_init(&vm);`) therefore had garbage profiler state,
 * and the first function call wrote through a garbage prof_state pointer --
 * src/vm/vm.c:3930 -> prof_record_call -> src/compilation/profiler.c:101
 * `fr->depth = depth`. On Windows that is 18 CTest cases dying with
 * 0xC0000005; on Linux it passed only because the fresh stack page happened to
 * be zero.
 *
 * Prefilling the struct with 0xAA and checking afterwards is what makes the
 * difference visible on every platform, instead of only on the platform whose
 * stack happens to be dirty. Delete the memset in vm_init and this probe fails
 * loudly instead of silently.
 */
#include "vm.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s\n", msg); failures++; } } while (0)

int main(void) {
    VM vm;
    memset(&vm, 0xAA, sizeof vm);
    vm_init(&vm);

    /* The two fields whose absence caused the crashes. */
    CHECK(vm.prof_enabled == 0, "prof_enabled");
    CHECK(vm.prof_state == NULL, "prof_state");

    /* Every other field the old list never mentioned. */
    CHECK(vm.hookCount == 0, "hookCount");
    CHECK(vm.spi_subs == NULL, "spi_subs");
    CHECK(vm.spi_sub_count == 0, "spi_sub_count");
    CHECK(vm.spi_sub_cap == 0, "spi_sub_cap");
    CHECK(vm.user_data == NULL, "user_data");
    CHECK(vm.argc == 0, "argc");
    CHECK(vm.argv == NULL, "argv");
    CHECK(vm.cur_argc == 0, "cur_argc");
    CHECK(vm.main_thread == NULL, "main_thread");
    for (int i = 0; i < 8; i++) CHECK(vm.mod_bcs[i] == NULL, "mod_bcs[i]");
    CHECK(vm.im2d_interval_ms == 0, "im2d_interval_ms");
    CHECK(vm.im2d_next_frame == 0, "im2d_next_frame");
    CHECK(vm.im2d_ready == 0, "im2d_ready");
    CHECK(vm.im2d_dt == 0.0, "im2d_dt");
    CHECK(vm.im2d_scene[0] == '\0', "im2d_scene");
    CHECK(vm.im2d_last_scene[0] == '\0', "im2d_last_scene");
    CHECK(vm.modCount == 0, "modCount");
    CHECK(vm.dbg_active == 0, "dbg_active");

    /* The non-zero defaults the explicit list must still win with -- zeroing
     * must not replace intent, only guarantee a starting value. */
    CHECK(vm.sp == -1, "sp");
    CHECK(vm.exec_timeout_ms == 120000, "exec_timeout_ms");
    CHECK(vm.mod_caps == -1, "mod_caps");
    CHECK(vm.record_save_path != NULL, "record_save_path");
    CHECK(vm.ent_free_head == -1, "ent_free_head");
    CHECK(vm.limit_vram == 0.0, "limit_vram");

    if (failures == 0) {
        vm_free(&vm);
        printf("vm_init_probe: OK\n");
        return 0;
    }
    /* When a check failed the VM is indeterminate by definition, so freeing it
     * would follow garbage pointers and abort before stdout is flushed -- which
     * is exactly how the first negative control of this probe lost its FAIL
     * lines to "free(): invalid pointer".  Report, then leave the process. */
    fflush(stdout);
    return 1;
}
