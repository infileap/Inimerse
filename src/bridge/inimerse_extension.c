/* inimerse_extension.c - the CPython extension module `inimerse`.
 *
 * docs/archive/RELEASE_0.5.0.md claimed a file named inimerse_extension.c
 * implementing PyInit_inimerse(); until now no such file existed anywhere in
 * the tree, so `import inimerse` failed on every machine.  This is that file.
 *
 * It is a real C extension (not a ctypes shim) driving the same C ABI the JNI
 * layer drives - src/bridge/bridge_abi.c - so the Python and Java sides report
 * identical values for identical inputs.  The three functions below are the
 * engine's own version string, the engine's own SHA-256, and the engine's own
 * lexer+parser; none of them echoes its argument back.
 */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <stdio.h>

#include "inimerse_bridge.h"

static PyObject *bridge_error(const char *fn, int code) {
    PyErr_Format(PyExc_RuntimeError, "inimerse: %s failed (code %d)", fn, code);
    return NULL;
}

static PyObject *py_version(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    const char *out = NULL;
    size_t len = 0;
    (void)self;
    if (inimerse_bridge_version(&out, &len) != 0) return bridge_error("version", 1);
    return PyUnicode_FromStringAndSize(out, (Py_ssize_t)len);
}

static PyObject *py_sha256_file(PyObject *self, PyObject *arg) {
    const char *path;
    Py_ssize_t path_len;
    const char *out = NULL;
    size_t out_len = 0;
    int rc;
    (void)self;

    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "sha256_file() expects a str path");
        return NULL;
    }
    path = PyUnicode_AsUTF8AndSize(arg, &path_len);
    if (!path) return NULL;
    rc = inimerse_bridge_sha256_file(path, (size_t)path_len, &out, &out_len);
    if (rc != 0) return bridge_error("sha256_file", rc);
    return PyUnicode_FromStringAndSize(out, (Py_ssize_t)out_len);
}

static PyObject *py_parse_count(PyObject *self, PyObject *arg) {
    const char *source;
    Py_ssize_t source_len;
    int32_t out = 0;
    int rc;
    (void)self;

    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "parse_count() expects a str of .im source");
        return NULL;
    }
    source = PyUnicode_AsUTF8AndSize(arg, &source_len);
    if (!source) return NULL;
    rc = inimerse_bridge_parse_count(source, (size_t)source_len, &out);
    if (rc != 0) return bridge_error("parse_count", rc);
    return PyLong_FromLong((long)out);
}

static PyMethodDef kMethods[] = {
    {"version", py_version, METH_NOARGS,
     "version() -> str\n\n"
     "The engine's own version string (INFIVERSE_VERSION)."},
    {"sha256_file", py_sha256_file, METH_O,
     "sha256_file(path) -> str\n\n"
     "Lowercase hex SHA-256 of a file, computed by the engine's hasher."},
    {"parse_count", py_parse_count, METH_O,
     "parse_count(source) -> int\n\n"
     "Top-level statement count from the engine's own lexer+parser."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef kModule = {
    PyModuleDef_HEAD_INIT,
    "inimerse",
    "Inimerse engine bridge: version / sha256_file / parse_count, backed by the\n"
    "engine's own C code rather than a re-implementation.",
    -1,
    kMethods
};

PyMODINIT_FUNC PyInit_inimerse(void) {
    PyObject *m = PyModule_Create(&kModule);
    PyObject *v;
    if (!m) return NULL;
    v = py_version(NULL, NULL);
    if (v && PyModule_AddObject(m, "__version__", v) < 0) {
        Py_DECREF(v);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}
