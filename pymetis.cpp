#include <vector>
#include <cassert>

#include <boost/range/algorithm.hpp>
#include <boost/range/numeric.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

extern "C" {
#include <metis.h>
}

namespace py = pybind11;

//---------------------------------------------------------------------------
void pointwise_graph(
        int n, int block_size,
        const std::vector<int> &ptr,
        const std::vector<int> &col,
        std::vector<int> &pptr,
        std::vector<int> &pcol
        )
{
    int np = n / block_size;

    assert(np * block_size == n);

    // Create pointwise matrix
    std::vector<int> ptr1(np + 1, 0);
    std::vector<int> marker(np, -1);
    for(int ip = 0, i = 0; ip < np; ++ip) {
        for(int k = 0; k < block_size; ++k, ++i) {
            for(int j = ptr[i]; j < ptr[i+1]; ++j) {
                int cp = col[j] / block_size;
                if (marker[cp] != ip) {
                    marker[cp] = ip;
                    ++ptr1[ip+1];
                }
            }
        }
    }

    boost::partial_sum(ptr1, ptr1.begin());
    boost::fill(marker, -1);

    std::vector<int> col1(ptr1.back());

    for(int ip = 0, i = 0; ip < np; ++ip) {
        int row_beg = ptr1[ip];
        int row_end = row_beg;

        for(int k = 0; k < block_size; ++k, ++i) {
            for(int j = ptr[i]; j < ptr[i+1]; ++j) {
                int cp = col[j] / block_size;

                if (marker[cp] < row_beg) {
                    marker[cp] = row_end;
                    col1[row_end++] = cp;
                }
            }
        }
    }


    // Transpose pointwise matrix
    int nnz = ptr1.back();

    std::vector<int> ptr2(np + 1, 0);
    std::vector<int> col2(nnz);

    for(int i = 0; i < nnz; ++i)
        ++( ptr2[ col1[i] + 1 ] );

    boost::partial_sum(ptr2, ptr2.begin());

    for(int i = 0; i < np; ++i)
        for(int j = ptr1[i]; j < ptr1[i+1]; ++j)
            col2[ptr2[col1[j]]++] = i;

    std::rotate(ptr2.begin(), ptr2.end() - 1, ptr2.end());
    ptr2.front() = 0;

    // Merge both matrices.
    boost::fill(marker, -1);
    pptr.resize(np + 1, 0);

    for(int i = 0; i < np; ++i) {
        for(int j = ptr1[i]; j < ptr1[i+1]; ++j) {
            int c = col1[j];
            if (marker[c] != i) {
                marker[c] = i;
                ++pptr[i + 1];
            }
        }

        for(int j = ptr2[i]; j < ptr2[i+1]; ++j) {
            int c = col2[j];
            if (marker[c] != i) {
                marker[c] = i;
                ++pptr[i + 1];
            }
        }
    }

    boost::partial_sum(pptr, pptr.begin());
    boost::fill(marker, -1);

    pcol.resize(pptr.back());

    for(int i = 0; i < np; ++i) {
        int row_beg = pptr[i];
        int row_end = row_beg;

        for(int j = ptr1[i]; j < ptr1[i+1]; ++j) {
            int c = col1[j];

            if (marker[c] < row_beg) {
                marker[c] = row_end;
                pcol[row_end++] = c;
            }
        }

        for(int j = ptr2[i]; j < ptr2[i+1]; ++j) {
            int c = col2[j];

            if (marker[c] < row_beg) {
                marker[c] = row_end;
                pcol[row_end++] = c;
            }
        }
    }
}

//---------------------------------------------------------------------------
std::vector<int> pointwise_partition(
        int npart,
        const std::vector<int> &ptr,
        const std::vector<int> &col
        )
{
    int nrows = ptr.size() - 1;

    std::vector<int> part(nrows);

    if (npart == 1) {
        boost::fill(part, 0);
    } else {
        int wgtflag = 0;
        int numflag = 0;
        int options = 0;
        int edgecut;

#if defined(METIS_VER_MAJOR) && (METIS_VER_MAJOR >= 5)
        int nconstraints = 1;
        METIS_PartGraphKway(
                &nrows, //nvtxs
                &nconstraints, //ncon -- new
                const_cast<int*>(ptr.data()), //xadj
                const_cast<int*>(col.data()), //adjncy
                NULL, //vwgt
                NULL, //vsize -- new
                NULL, //adjwgt
                &npart,
                NULL,//real t *tpwgts,
                NULL,// real t ubvec
                NULL,
                &edgecut,
                part.data()
                );
#else
        METIS_PartGraphKway(
                &nrows,
                const_cast<int*>(ptr.data()),
                const_cast<int*>(col.data()),
                NULL,
                NULL,
                &wgtflag,
                &numflag,
                &npart,
                &options,
                &edgecut,
                part.data()
                );
#endif
    }

    return part;
}

//---------------------------------------------------------------------------
py::array_t<int> partition(
        int nparts, int block_size,
        py::array_t<int> &_ptr,
        py::array_t<int> &_col
        )
{
    py::buffer_info pinfo = _ptr.request();
    py::buffer_info cinfo = _col.request();

    std::vector<int> ptr(
            static_cast<const int*>(pinfo.ptr),
            static_cast<const int*>(pinfo.ptr) + pinfo.shape[0]
            );

    std::vector<int> col(
            static_cast<const int*>(cinfo.ptr),
            static_cast<const int*>(cinfo.ptr) + cinfo.shape[0]
            );

    int n = ptr.size() - 1;

    // Pointwise graph
    std::vector<int> pptr, pcol;
    pointwise_graph(n, block_size, ptr, col, pptr, pcol);

    // Pointwise partition
    std::vector<int> ppart = pointwise_partition(nparts, pptr, pcol);

    std::vector<int> part(n);
    for(int i = 0; i < n; ++i)
        part[i] = ppart[i / block_size];

    return py::array(part.size(), part.data());
}

//---------------------------------------------------------------------------
PYBIND11_PLUGIN(pymetis)
{
    py::module m("pymetis", "Python interface to metis");
    m.def("partition", partition);
    return m.ptr();
}
