#include <Python.h>
#include <assert.h>
#include <inttypes.h>
#include "common.h"
#include "bigWig.h"
#include <math.h>
#include "pyBigWig.h"

//Need to add proper error handling rather than just assert()
PyObject* bwOpen(PyObject *self, PyObject *pyFname) {
    char *fname = NULL;
    struct bbiFile *bw;
    struct bbiChromInfo *ci, *p;
    PyObject *h, *val;
    bigWigFile_t *ret;
    
    if(!PyArg_ParseTuple(pyFname, "s", &fname)) return NULL;

    //Check to ensure that the file exists, since bigWigFileOpen() will crash python if not!
    if(access(fname, R_OK) == -1) {
        printf("%s does not exist!\n", fname);
        Py_INCREF(Py_None);
        return Py_None;
    } else {
        if(!(bw = bigWigFileOpen(fname))) {
            Py_INCREF(Py_None);
            return Py_None;
        }
    }

    //Load the chromosome table, which is a linked list (annoyingly)
    if(!(ci = bbiChromList(bw))) {
        bigWigFileClose(&bw);
        Py_INCREF(Py_None);
        return Py_None;
    }

    //Construct the output object
    if(!(h = PyDict_New())) {
        bigWigFileClose(&bw); 
        Py_INCREF(Py_None);
        return Py_None;
    }
    p = ci;
    while(p) {
        val = PyLong_FromUnsignedLong(p->size);
        if(!val) {
            printf("[pyBigWig] Couldn't create a python object to hold a chromosome length!\n");
            goto error;
        }
        if(PyDict_SetItemString(h, p->name, val) == -1) { //valgrind complains here
            printf("[pyBigWig] An error occurred while creating the chroms dictionary!\n");
            goto error;
        }
        Py_DECREF(val);
        p = p->next;
    }
    bbiChromInfoFreeList(&ci);

    ret = PyObject_New(bigWigFile_t, &bigWigFile);
    ret->bbi = bw;
    ret->chroms = h;
    PyObject_GC_Init((PyObject*) ret);
    return (PyObject*) ret;

error:
    Py_DECREF(h);
    Py_XDECREF(val);
    bbiChromInfoFreeList(&ci);
    bigWigFileClose(&bw);
    Py_INCREF(Py_None);
    return Py_None;
}

static void bwDealloc(bigWigFile_t *self) {
    PyObject_GC_Fini((PyObject*) self);
    Py_XDECREF(self->chroms);
    if(self->bbi) bigWigFileClose(&(self->bbi));
    PyObject_DEL(PyObject_AS_GC(self));
}

static PyObject *bwClose(bigWigFile_t *self, PyObject *args) {
    bigWigFileClose(&(self->bbi));
    self->bbi = NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

//Accessor for the chroms, args is optional
static PyObject *bwGetChroms(bigWigFile_t *self, PyObject *args) {
    PyObject *ret = Py_None, *h, *s;
    char *chrom = NULL;

    if(!(PyArg_ParseTuple(args, "|s", &chrom))) {
        h = PyDictProxy_New(self->chroms);
        ret = Py_BuildValue("O",h);
        Py_DECREF(h);
    } else if(!chrom) {
        h = PyDictProxy_New(self->chroms);
        ret = Py_BuildValue("O",h);
        Py_DECREF(h);
    } else {
        s = PyString_FromString(chrom);
        if(PyDict_Contains(self->chroms, s))
            ret = PyDict_GetItemString(self->chroms, chrom);
        Py_DECREF(s);
    }

    Py_INCREF(ret);
    return ret;
}

//Fetch a value or values for a single range
static PyObject *bwGetValRange(bigWigFile_t *self, PyObject *args, PyObject *kwds) {
    double *val;
    uint32_t start, end;
    static char *kwd_list[] = {"chrom", "start", "end", "type", "nBins", NULL};
    char *chrom, *type = "mean";
    PyObject *ret;
    int iType, i, nBins = 1;

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "skk|si", kwd_list, &chrom, &start, &end, &type, &nBins)) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if(!nBins) nBins = 1; //For some reason, not specifying this overrides the default!

    iType = bbiSummaryTypeFromString(type);
    val = malloc(nBins*sizeof(double));
    for(i=0; i<nBins; i++) val[i] = strtod("NaN", NULL);

    if(!bigWigSummaryArray(self->bbi, chrom, start, end, iType, nBins, val)) {
        free(val);
        Py_INCREF(Py_None);
        return Py_None;
    }

    ret = PyList_New(nBins);
    for(i=0; i<nBins; i++) PyList_SetItem(ret, i, (isnan(val[i]))?Py_None:PyFloat_FromDouble(val[i]));
    free(val);

    Py_INCREF(ret);
    return ret;
}

PyMODINIT_FUNC initpyBigWig(void) {
    if(PyType_Ready(&bigWigFile) < 0) return;

    PyObject *mod = Py_InitModule3("pyBigWig", bwMethods, "A module for handling bigWig files");
    if(!mod) return;

    Py_INCREF(&bigWigFile);
    PyModule_AddObject(mod, "bigWigFile", (PyObject*)&bigWigFile);
}