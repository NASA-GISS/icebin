#include <cstdio>
#include <unordered_set>

#include <spsparse/netcdf.hpp>
#include <icebin/GCMRegridder.hpp>
#include <icebin/IceRegridder_L0.hpp>

using namespace netCDF;
using namespace ibmisc;
using namespace std::placeholders;  // for _1, _2, _3...

namespace icebin {

// -----------------------------------------------------
IceRegridder::IceRegridder() : interp_style(InterpStyle::Z_INTERP), name("icesheet") {}

IceRegridder::~IceRegridder() {}


std::unordered_map<long,double> IceRegridder::elevI_hash()
{
	// Convert elevI to a hash table, so we can look up in it easily
	std::unordered_map<long,double> elevIh;
	for (auto ii=elevI.begin(); ii != elevI.end(); ++ii)
		elevIh.insert(std::make_pair(ii.index(0), ii.val()));
	return elevIh;
}
// --------------------------------------------------------

// ==============================================================
// Write out the parts that this class computed --- so we can test/check them
// -------------------------------------------------------------
/** Produces the diagonal matrix [Atmosphere projected] <-- [Atmosphere]
NOTE: wAvAp == sApvA */
void IceRegridder::wAvAp(SparseVector &w)
{
	ibmisc::Proj_LL2XY proj(gridI->sproj);
	for (auto cell=gcm->gridA->cells.begin(); cell != gcm->gridA->cells.end(); ++cell) {
		w.add({cell->index}, cell->native_area / cell->proj_area(&proj));
	}
}

/** Produces the diagonal matrix [Atmosphere projected] <-- [Atmosphere]
NOTE: wAvAp == sApvA */
void IceRegridder::wEvEp(SparseVector &w)
{
	ibmisc::Proj_LL2XY proj(gridI->sproj);
	for (auto cell=gcm->gridA->cells.begin(); cell != gcm->gridA->cells.end(); ++cell) {
		long nhp = gcm->nhp(cell->index);
		long tuple[2] = {cell->index, 0};
		long &ihp(tuple[1]);
		for (ihp=0; ihp<nhp; ++ihp) {
			long indexE = gcm->indexingHP.tuple_to_index(tuple);
			w.add({indexE}, cell->native_area / cell->proj_area(&proj));
		}
	}
}
// -------------------------------------------------------------
void IceRegridder::clear()
{
	gridI.reset();
	exgrid.reset();
	elevI.clear();
}
// -------------------------------------------------------------
void IceRegridder::ncio(NcIO &ncio, std::string const &vname)
{
	if (ncio.rw == 'r') clear();

	auto info_v = get_or_add_var(ncio, vname + ".info", netCDF::ncInt64, {});
	get_or_put_att(info_v, ncio.rw, "name", name);
	get_or_put_att_enum(info_v, ncio.rw, "interp_style", interp_style);

	gridI->ncio(ncio, vname + ".gridI");
	exgrid->ncio(ncio, vname + ".exgrid");
	ncio_spsparse(ncio, elevI, true, vname + ".elevI");
}
// ========================================================
void GCMRegridder::init(
	std::unique_ptr<Grid> &&_gridA,
	ibmisc::Domain<int> &&_domainA,		// Tells us which cells in gridA to keep...
	std::vector<double> &&_hpdefs,	// [nhp]
	ibmisc::Indexing<long,long> &&_indexingHP,
	bool _correctA)
{
	gridA = std::move(_gridA);
	domainA = std::move(_domainA);
	hpdefs = std::move(_hpdefs);
	indexingHP = std::move(_indexingHP);
	correctA = _correctA;

	if (indexingHP.rank() != 2) (*icebin_error)(-1,
		"indexingHP has rank %d, it must have rank=2", indexingHP.rank());
}
// -------------------------------------------------------------
std::unique_ptr<IceRegridder> new_ice_regridder(IceRegridder::Type type)
{
	switch(type.index()) {
		case IceRegridder::Type::L0 :
			return std::unique_ptr<IceRegridder>(new IceRegridder_L0);
		break;
		default :
			(*icebin_error)(-1,
				"Unknown IceRegridder::Type %s", type.str());
		break;
	}
}
// -------------------------------------------------------------
// ==============================================================

void GCMRegridder::clear()
{
	gridA.reset();
	hpdefs.clear();
	sheets.clear();
}

void GCMRegridder::ncio(NcIO &ncio, std::string const &vname)
{
	if (ncio.rw == 'r') clear();
	auto info_v = get_or_add_var(ncio, vname + ".info", netCDF::ncInt64, {});
	// get_or_put_att_enum(info_v, ncio.rw, "type", type);

	gridA->ncio(ncio, vname + ".gridA");
	indexingHP.ncio(ncio, ncInt, vname + ".indexingHP");
	ncio_vector(ncio, hpdefs, true, vname + ".hpdefs", ncDouble,
		get_or_add_dims(ncio, {vname + ".nhp"}, {hpdefs.size()} ));
	domainA.ncio(ncio, ncInt, vname + ".domainA");
	get_or_put_att(info_v, ncio.rw, vname + ".correctA", &correctA, 1);


	// Read/Write the Main Sheets
	if (ncio.rw == 'r') {
		std::vector<std::string> sheet_names;
		get_or_put_att(info_v, ncio.rw, vname + ".sheets", ncString, sheet_names);

		for (auto sheet_name : sheet_names) {
			std::string sheet_vname = vname + "." + sheet_name;
			IceRegridder::Type sheet_type;
			auto sheet_info_v = get_or_add_var(ncio, sheet_vname + ".info", netCDF::ncInt64, {});

			get_or_put_att_enum(sheet_info_v, ncio.rw, "type", sheet_type);
			std::unique_ptr<IceRegridder> sheet(new_ice_regridder(sheet_type));
			sheet->ncio(ncio, vname + "." + sheet_name);
			add_sheet(std::move(sheet));
		}
	} else {	// Write
		for (auto ii=sheets.begin(); ii != sheets.end(); ++ii) {
			IceRegridder *sheet = &*(ii->second);
			sheet->ncio(ncio, vname + "." + sheet->name);
		}
	}
}
// -------------------------------------------------------------

// ---------------------------------------------------------------------
/** Made for binding... */
static bool in_good(std::unordered_set<long> const *set, long index_c)
{
	return (set->find(index_c) != set->end());
}

void IceRegridder::filter_cellsA(std::function<bool (long)> const &useA)
{

  // Figure out which cells to keep

	// List of cells in gridI / exgrid that overlap a cell we want to keep
	std::unordered_set<long> good_index_gridI;
	std::unordered_set<long> good_index_exgrid;


	std::unordered_set<int> good_j;
	for (auto excell = exgrid->cells.begin(); excell != exgrid->cells.end(); ++excell) {
		int index1 = excell->i;
		if (useA(index1)) {
			good_index_gridI.insert(excell->j);
			good_index_exgrid.insert(excell->index);
		}
	}

	// Remove unneeded cells from gridI
	gridI->filter_cells(std::bind(&in_good, &good_index_gridI, _1));
	exgrid->filter_cells(std::bind(&in_good, &good_index_exgrid, _1));
}

void GCMRegridder::filter_cellsA(std::function<bool(long)> const &keepA)
{

	// Now remove cells from the exgrids and gridIs that
	// do not interact with the cells we've kept in grid1.
	for (auto sheet=sheets.begin(); sheet != sheets.end(); ++sheet) {
		sheet->second->filter_cellsA(keepA);
	}

	gridA->filter_cells(keepA);
}
// ---------------------------------------------------------------------
void IceRegridder::init(
	std::string const &_name,
	std::unique_ptr<Grid> &&_gridI,
	std::unique_ptr<Grid> &&_exgrid,
	InterpStyle _interp_style,
	SparseVector &&elevI)
{
	name = (_name != "" ? _name : gridI->name);
	gridI = std::move(_gridI);
	exgrid = std::move(_exgrid);
	interp_style = _interp_style;
	elevI = std::move(elevI);
}
// ==============================================================
/** Gives weights for linear interpolation with a bunch of points.
If our point is off the end of the range, just continue the slope in extrapolation.
@param xpoints This is not blitz::Array<double,1> because Blitz++ does not (yet) implement STL-compatibl
e iterators. */
extern void linterp_1d(
	std::vector<double> const &xpoints,
	double xx,
	int *indices, double *weights)  // Size-2 arrays
{
	int n = xpoints.size();

	// This is the point ABOVE our value.
	// (i0 = i1 - 1, xpoints[i0] < xx <= xpoints[i1])
	// See: http://www.cplusplus.com/reference/algorithm/lower_bound/
	int i1 = lower_bound(xpoints.begin(), xpoints.end(), xx) - xpoints.begin();

	if (i1 <= 0) i1 = 1;
	if (i1 >= n) i1 = n-1;

	int i0 = i1-1;
	indices[0] = i0;
	indices[1] = i1;
	double ratio = (xx - xpoints[i0]) / (xpoints[i1] - xpoints[i0]);
	weights[0] = (1.0 - ratio);
	weights[1] = ratio;
}




}	// namespace icebin