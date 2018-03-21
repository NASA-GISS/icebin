#include <cstdio>
#include <prettyprint.hpp>
#include <ibmisc/netcdf.hpp>
#include <ibmisc/runlength.hpp>
#include <icebin/error.hpp>
#include <ibmisc/linear/linear.hpp>
#include <ibmisc/linear/compressed.hpp>

using namespace std;
using namespace ibmisc;
using namespace netCDF;
using namespace icebin;

static double const NaN = std::numeric_limits<double>::quiet_NaN();


template<class T, int len>
blitz::TinyVector<T, len> vector_to_tiny(std::vector<T> const &vec)
{
    if (vec.size() != len) {
        (*ibmisc::ibmisc_error)(-1,
            "vector_to_tiny(): vector length %ld does not match declared length %d\n", vec.size(), len);
    }

    blitz::TinyVector<T, len> ret;
    for (int i=0; i < len; ++i) {
        ret[i] = vec[i];
    }
    return ret;
}


void combine_chunks(
    std::vector<std::string> const &ifnames,    // Names of input chunks
    std::string const &ofname,
    char ofmode,    // 'w' or 'a' for write mode of output file
    std::array<std::string,2> const &sgrids)    // {"B", "A"} --> matrix BvA
{
    printf("======== BEGIN combine_chunks(%sv%s)\n", sgrids[0].c_str(), sgrids[1].c_str());

    // Names of the variables we will read/write
    std::string BvA = sgrids[0] + "v" + sgrids[1];

    // Get total sizes
    int nnz = 0;        // = number non-zero]
    std::array<long,2> sparse_extents;
    std::array<std::vector<int>,2> shapes;
    std::vector<size_t> sizes;    // For printing
    for (std::string const &ifname : ifnames) {
        NcIO ncio(ifname, 'r');
        size_t sz = ncio.nc->getDim(BvA+".M.nnz").getSize();
        nnz += sz;
        sizes.push_back(sz);

        for (int i=0; i<2; ++i) {
            NcVar nc_dimA(ncio.nc->getVar("dim" + sgrids[i]));
            nc_dimA.getAtt("sparse_extent").getValues(&sparse_extents[i]);
            get_or_put_att(nc_dimA, 'r', "shape", "int", shapes[i]);
        }
    }
    cout << "Total non-zero elements in matrix = " << sizes << " = " << nnz << endl;
    cout << "sparse_extents = " << sparse_extents << endl;


    // Allocate
    linear::Weighted_Compressed ret;

    {auto wM(ret.weights[0].accum());
    auto M(ret.M.accum());
    auto Mw(ret.weights[1].accum());

        // Aggregate them together
        int iM=0;
        for (std::string const &ifname : ifnames) {
            printf("--- Reading %s\n", ifname.c_str());
            NcIO ncio(ifname, 'r');

            auto dimB(nc_read_blitz<int,1>(ncio.nc, "dim"+sgrids[0]));
            auto dimA(nc_read_blitz<int,1>(ncio.nc, "dim"+sgrids[1]));

            auto wM_d(nc_read_blitz<double,1>(ncio.nc, BvA+".wM"));
            for (int i=0; i<wM_d.extent(0); ++i) wM.add({dimB(i)}, wM_d(i));

            auto indices_d(nc_read_blitz<int,2>(ncio.nc, BvA+".M.indices"));
            auto values_d(nc_read_blitz<double,1>(ncio.nc, BvA+".M.values"));
            for (int i=0; i<values_d.extent(0); ++i) {
                M.add({dimB(indices_d(i,0)), dimA(indices_d(i,1))}, values_d(i));
            }

            auto Mw_d(nc_read_blitz<double,1>(ncio.nc, BvA+".Mw"));
            for (int i=0; i<Mw_d.extent(0); ++i) Mw.add({dimA(i)}, Mw_d(i));
        }

        // Check
        if (ret.M.nnz() != nnz) (*icebin_error)(-1, "Bad count: %d vs %d", iM, nnz);
    }    // Finish off accumulators

    // Write it out
    printf("---- Writing output to %s\n", ofname.c_str());
    {NcIO ncio(ofname, ofmode);
        ret.ncio(ncio, BvA);
    }
}

int main(int argc, char **argv)
{
    // Save args as C++ vector
    vector<string> ifnames;
    for (int i=1; i<argc; ++i) ifnames.push_back(string(argv[i]));

    // Parse the output name
    char *dash = rindex(argv[1], '-');
    std::string ofname(argv[1], dash);

#if 1
    vector<array<string,2>> sgridss {
        {"I","E"}, {"E","I"},
        {"I","A"}, {"A","I"},
        {"A","E"},
        {"I2", "A"}, {"I2", "E"}};

    char ofmode = 'w';
#else
    vector<array<string,2>> sgridss {
        {"I2", "A"}, {"I2", "E"}};

    char ofmode = 'a';
#endif

    for (auto const &sgrids : sgridss) {
        combine_chunks(ifnames, ofname, ofmode, sgrids);
        ofmode = 'a';
    }

    return 0;
}