#pragma once

#include <boost/function.hpp>
#include <blitz/array.h>
#include <giss/SparseMatrix.hpp>

namespace glint2 {

// ------------------------------------------------

// ------------------------------------------------

/** Serves two purposes: (1) Translate global grid cell indices to
local indexing scheme, for a particular GCM MPI node. (2) Tells
whether the grid cell is in the node's domain or halo. */
class GridDomain {
public:

	/** Number of indices used locally (by the GCM) to index a grid cell
	(for ModelE, this is generally 2, cells are inexed by i,j) */
	const int num_local_indices;

	GridDomain(int _num_local_indices) : num_local_indices(_num_local_indices) {}
	virtual ~GridDomain() {}

	/** Convert to local indexing scheme for this MPI node. */
	virtual void global_to_local(int gindex_c, int *lindex) const = 0;

	/** Tells whether a grid cell is in the MPI node's domain.
	@param lindex Result of global_to_local() */
	virtual bool in_domain(int *lindex) const = 0;

	/** Tells whether a grid cell is in the MPI node's halo (or main domain).
	@param lindex Result of global_to_local() */
	virtual bool in_halo(int *lindex) const = 0;

	bool in_halo2(int gindex_c) const
	{
		int lindex[num_local_indices];
		global_to_local(gindex_c, lindex);
		return in_halo(lindex);
	}

	/** Default implementation is OK; or re-implement to avoid
	going through extra virtual function call
	@return The in_halo() function */
	virtual boost::function<bool (int)> get_in_halo2() const;

#if 0
	void global_to_local(
		blitz::Array<double,1> const &global,
		std::vector<blitz::Array<double,1>> &olocal);
#endif

};
// ------------------------------------------------
/** The "NOP" GridDomain.  Does not translate indices. */
class GridDomain_Identity : public GridDomain
{
public:
	GridDomain_Identity() : GridDomain(1) 
	{
		printf("new GridDomain_Identity()\n");
	}

	void global_to_local(int gindex_c, int *lindex) const
	{
		lindex[0] = gindex_c;
	}

	bool in_domain(int *lindex) const
		{ return true; }
	bool in_halo(int *lindex) const
	{
		return true;
	}
};

// ==========================================================

#if 0
extern std::unique_ptr<giss::VectorSparseMatrix> filter_matrix(
	GridDomain const &domain1,
	GridDomain const &domain2,
	giss::VectorSparseMatrix const &mat);
#endif

}