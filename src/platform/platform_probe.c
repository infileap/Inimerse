#include "platform.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define PP_RMDIR(d) _rmdir(d)
#else
#include <unistd.h>
#define PP_RMDIR(d) rmdir(d)
#endif

int main(void) {
    char path[4096] = {0};
    printf("platform_time_ms=%llu\n", (unsigned long long)im_platform_now_ms());
    printf("platform_executable=%s\n", im_platform_executable_path(path, sizeof(path)) >= 0 ? path : "unknown");
    if (im_platform_now_ms() == 0) return 3;
    if (im_platform_path_join(path, sizeof(path), "/tmp", "inimerse") != 0) return 4;
    if (im_platform_has_capability("definitely_missing_capability")) return 5;
    /* A join that does not fit refuses instead of truncating: a silently
       truncated path writes the file to the wrong place, whereas a refused join
       is a skipped entry.  Callers must treat -1 as "do not proceed".
       See docs/AUDIT.md 1.22. */
    char tiny[8];
    if (im_platform_path_join(tiny, sizeof tiny, "/tmp", "inimerse") != -1) return 6;
    if (im_platform_path_join(NULL, sizeof tiny, "/tmp", "inimerse") != -1) return 7;
    if (im_platform_path_join(tiny, 0, "/tmp", "inimerse") != -1) return 8;
    /* Redundant separators are normalised, so "a/" + "/b" and "a" + "b" agree. */
    char a[64], b[64];
    if (im_platform_path_join(a, sizeof a, "/tmp/", "/inimerse") != 0) return 9;
    if (im_platform_path_join(b, sizeof b, "/tmp", "inimerse") != 0) return 10;
    if (strcmp(a, b) != 0) return 11;
    /* im_platform_mkdirs contract: a directory that is already there is
       success, not failure; the path cap refuses rather than truncates.  This
       is the engine's one production point for "make this directory tree", and
       the verse core repeats the same rule in vl_mkdir_p (src/verse/layer.c)
       because it stays free of src/platform.  The verse copy's existence test
       used to be `fopen(dir, "r")`, which cannot open a directory on Windows,
       so the SECOND call on the same path answered -1 and callers reported
       "cannot create laws" / VL_ERR_IO for a directory they had just created
       themselves (pinned by verse_layer_probe).  A prefix that is a Windows
       drive designator ("C:") must never be handed to mkdir either: _mkdir("C:")
       answers EACCES when the drive has no current directory, which made the
       outcome depend on where the process was started from.  See docs/AUDIT.md
       1.29. */
    if (im_platform_mkdirs("im_pp_mk") != 0) return 12;
    if (im_platform_mkdirs("im_pp_mk") != 0) return 13;             /* already there */
    if (im_platform_mkdirs("im_pp_mk/a/b/c") != 0) return 14;       /* deep and new */
    if (im_platform_mkdirs("im_pp_mk/a/b/c") != 0) return 15;       /* deep, again */
    if (im_platform_mkdirs("im_pp_mk/") != 0) return 16;            /* trailing separator */
    if (im_platform_mkdirs(NULL) != -1) return 17;
    if (im_platform_mkdirs("") != -1) return 18;
    /* A path at or over the buffer cap is refused, never truncated: a truncated
       path creates the directory somewhere else entirely. */
    char deep[4200];
    memset(deep, 'x', sizeof deep - 1);
    deep[sizeof deep - 1] = '\0';
    if (im_platform_mkdirs(deep) != -1) return 19;
    /* im_platform_write_file answers 0 on success and -1 on failure.  It was
       written as `int ok = (...) ? 0 : -1; if (ok) return 0; return -1;`, which
       inverted the two: a good write answered -1 and a failed write answered 0.
       Its only caller (src/mod/server_mod_posix.c:89) reads non-zero as
       failure, so server_start killed the child it had just spawned and
       answered 0 instead of the room number on every POSIX run.  Both
       directions are pinned here -- a write that must succeed, the read that
       must see it, and a write to an unopenable path, which must refuse.
       See docs/AUDIT.md 1.44. */
    if (im_platform_write_file("im_pp_wf.txt", "abc", 3) != 0) return 20;
    char back[8] = {0};
    if (im_platform_read_file("im_pp_wf.txt", back, sizeof back) != 3) return 21;
    if (strcmp(back, "abc") != 0) return 22;
    if (im_platform_write_file("im_pp_mk/nope/x.txt", "abc", 3) != -1) return 23;
    remove("im_pp_wf.txt");
    PP_RMDIR("im_pp_mk/a/b/c"); PP_RMDIR("im_pp_mk/a/b"); PP_RMDIR("im_pp_mk/a"); PP_RMDIR("im_pp_mk");
    return 0;
}
