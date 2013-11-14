#include "qsort_r.h"
#include <stdlib.h>

void *_qsort_thunk = NULL;
int (*_qsort_saved_func)(void *, const void *, const void *) = NULL;

static int _qsort_comparator(const void *a, const void *b);

static int _qsort_comparator(const void *a, const void *b)
{
  return _qsort_saved_func(_qsort_thunk, a, b);
}

void
qsort_r_local(void *base, size_t nmemb, size_t size, void *thunk,
    int (*compar)(void *, const void *, const void *))
{
  _qsort_thunk = thunk;
  _qsort_saved_func = compar;

  qsort(base, nmemb, size, _qsort_comparator);
}

