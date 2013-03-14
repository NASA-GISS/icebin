#include <giss/ncutil.hpp>
#include <glint2/MatrixMaker.hpp>
#include <glint2/IceSheet_L0.hpp>
#include <glint2/HCIndex.hpp>

namespace glint2 {

void MatrixMaker::clear()
{
	sheets.clear();
	sheets_by_id.clear();
	grid1.reset();
	mask1.reset();
	hpdefs.clear();
	// hcmax.clear();
}

void MatrixMaker::realize() {

	// ---------- Check array bounds
	long n1 = grid1->ndata();
	if (mask1.get() && mask1->extent(0) != n1) {
		fprintf(stderr, "mask1 for %s has wrong size: %d (vs %d expected)\n",
			mask1->extent(0), n1);
		throw std::exception();
	}

	long nhc = hpdefs.size();
	if (hcmax.extent(0) != nhc-1) {
		fprintf(stderr, "hcmax for %s has wrong size: %d (vs %d expected)\n",
			mask1->extent(0), n1);
		throw std::exception();
	}

	// ------------- Realize the ice sheets
	for (auto sheet=sheets.begin(); sheet != sheets.end(); ++sheet)
		sheet->realize();
}

int MatrixMaker::add_ice_sheet(std::unique_ptr<IceSheet> &&sheet)
{
	if (sheet->name == "") {
		fprintf(stderr, "MatrixMaker::add_ice_sheet(): Sheet must have a name\n");
		throw std::exception();
	}

	int const index = _next_sheet_index++;
	sheet->index = index;
	sheet->gcm = this;
	
	sheets_by_id.insert(std::make_pair(sheet->index, sheet.get()));
	sheets.insert(sheet->name, std::move(sheet));
	return index;
}


/** NOTE: Does not necessarily assume that ice sheets do not overlap on the same GCM grid cell */
void MatrixMaker::compute_fhc(
	giss::CooVector<std::pair<int,int>,double> &fhc1h,	// std::pair<i1, hc>
	giss::CooVector<int,double> &fgice1)
{
	// Accumulate areas over all ice sheets
	giss::SparseAccumulator<int,double> area1_m;
	giss::SparseAccumulator<int,double> area1_m_hc;
	for (auto sheet = sheets.begin(); sheet != sheets.end(); ++sheet) {
		sheet->accum_areas(area1_m, area1_m_hc);
	}

	// Summing duplicates on area1_m and area1_m_hc not needed
	// because the unordered_map sums them automatically.

	// Compute fhc1h
	fhc1h.clear();
	HCIndex hc_index(n1());
	for (auto ii = area1_m_hc.begin(); ii != area1_m_hc.end(); ++ii) {
		int i1hc = ii->first;

		// Separate out into grid cell and height class
		int i1, hc;
		hc_index.index_to_ik(i1hc, i1, hc);

		fhc1h.add(std::make_pair(i1, hc), ii->second / area1_m[i1]);
	}
	fhc1h.sort();

	// Compute fgice1
	fgice1.clear();
	for (auto ii = area1_m.begin(); ii != area1_m.end(); ++ii) {
		int i1 = ii->first;
		double ice_covered_area = ii->second;
		fgice1.add(i1, ice_covered_area / grid1->get_cell(i1)->area);
	}
	fgice1.sort();
}

std::unique_ptr<giss::VectorSparseMatrix> MatrixMaker::hp_to_hc()
{
	std::unique_ptr<giss::VectorSparseMatrix> ret(
		new giss::VectorSparseMatrix(
		giss::SparseDescr(nhc(), nhc())));

	// Compute the hp->ice and ice->hc transformations for each ice sheet
	// and combine into one hp->hc matrix for all ice sheets.
	giss::SparseAccumulator<int,double> accum;
	giss::SparseAccumulator<int,double> *area1_m_hc = &accum;
	for (auto sheet = sheets.begin(); sheet != sheets.end(); ++sheet) {
		giss::VectorSparseMatrix &hp_to_ice = sheet->hp_to_ice();
		auto ice_to_hc(sheet->ice_to_hc(*area1_m_hc));
		ret->append(*multiply(*ice_to_hc, hp_to_ice));
	}

	// Compute 1 / area1_m_hc
	for (auto ii = area1_m_hc->begin(); ii != area1_m_hc->end(); ++ii)
		ii->second = 1.0d / ii->second;
	giss::SparseAccumulator<int,double> *area1_m_hc_inv = area1_m_hc;
	area1_m_hc = 0;

	// Divide by area1_m_hc
	for (auto ii = ret->begin(); ii != ret->end(); ++ii)
		ii.val() *= (*area1_m_hc_inv)[ii.col()];

	ret->sum_duplicates();

	return ret;
}
// --------------------------------------------------------------
// ==============================================================
// Write out the parts that this class computed --- so we can test/check them

boost::function<void ()> MatrixMaker::netcdf_define(NcFile &nc, std::string const &vname) const
{
	std::vector<boost::function<void ()>> fns;
	fns.reserve(sheets.size() + 1);

printf("MatrixMaker::netcdf_define(%s) (BEGIN)\n", vname.c_str());

	// ------ Attributes
	auto one_dim = giss::get_or_add_dim(nc, "one", 1);
	NcVar *info_var = nc.add_var((vname + ".info").c_str(), ncInt, one_dim);

	// Names of the ice sheets
	std::string sheet_names = "";
	for (auto sheet = sheets.begin(); ; ) {
		sheet_names.append(sheet->name);
		++sheet;
		if (sheet == sheets.end()) break;
		sheet_names.append(",");
	}
	info_var->add_att("sheetnames", sheet_names.c_str());
#if 0
		info_var->add_att("grid1.name", gcm->grid1->name.c_str());
		info_var->add_att("grid2.name", grid2->name.c_str());
		info_var->add_att("exgrid.name", exgrid->name.c_str());
#endif

	// Define the variables
	fns.push_back(grid1->netcdf_define(nc, vname + ".grid1"));
	if (mask1.get())
		fns.push_back(giss::netcdf_define(nc, vname + "mask1", *mask1));
	fns.push_back(giss::netcdf_define(nc, vname + ".hpdefs", hpdefs));
printf("***************** 1 hcmax.extent(0) = %d\n", hcmax.extent(0));
	fns.push_back(giss::netcdf_define(nc, vname + ".hcmax", hcmax));
	for (auto sheet = sheets.begin(); sheet != sheets.end(); ++sheet) {
		fns.push_back(sheet->netcdf_define(nc, vname + "." + sheet->name));
	}


printf("MatrixMaker::netcdf_define(%s) (END)\n", vname.c_str());

	return boost::bind(&giss::netcdf_write_functions, fns);
}
// -------------------------------------------------------------
static std::vector<std::string> parse_comma_list(std::string list)
{
	std::stringstream ss(list);
	std::vector<std::string> result;

	while( ss.good() ) {
		std::string substr;
		getline( ss, substr, ',' );
		result.push_back( substr );
	}
	return result;
}

std::unique_ptr<IceSheet> read_icesheet(NcFile &nc, std::string const &vname)
{
	auto info_var = nc.get_var((vname + ".info").c_str());
	std::string stype(giss::get_att(info_var, "parameterization")->as_string(0));

	std::unique_ptr<IceSheet> sheet;
	if (stype == "L0") {
		sheet.reset(new IceSheet_L0);
	}
#if 0
	else if (stype == "L1") {
		sheet.reset(new IceSheet_L1);
	}
#endif

	sheet->read_from_netcdf(nc, vname);
	printf("read_icesheet(%s) END\n", vname.c_str());
	return sheet;

}


void MatrixMaker::read_from_netcdf(NcFile &nc, std::string const &vname)
{
	clear();

	printf("MatrixMaker::read_from_netcdf(%s) 1\n", vname.c_str());
	grid1.reset(read_grid(nc, vname + ".grid1").release());
	if (giss::get_var_safe(nc, vname + ".mask1")) {
		mask1.reset(new blitz::Array<int,1>(
		giss::read_blitz<int,1>(nc, vname + ".mask1")));
	}
	hpdefs = giss::read_vector<double>(nc, vname + ".hpdefs");
	hcmax.reference(giss::read_blitz<double,1>(nc, vname + ".hcmax"));

	printf("MatrixMaker::read_from_netcdf(%s) 2\n", vname.c_str());

//	grid2.reset(read_grid(nc, "grid2").release());
//	exgrid.reset(read_grid(nc, "exgrid").release());

	// Read list of ice sheets
	NcVar *info_var = nc.get_var((vname + ".info").c_str());
	std::vector<std::string> sheet_names = parse_comma_list(std::string(
		giss::get_att(info_var, "sheetnames")->as_string(0)));

	for (auto sname = sheet_names.begin(); sname != sheet_names.end(); ++sname) {
		std::string var_name(vname + "." + *sname);
		printf("MatrixMaker::read_from_netcdf(%s) %s 3\n",
			vname.c_str(), var_name.c_str());
		add_ice_sheet(read_icesheet(nc, var_name));
	}

	// Remove grid cells that are not part of this domain.
	// TODO: This should be done while reading the cells in the first place.
	boost::function<bool (int)> include_cell1(domain->get_in_halo2());
	grid1->filter_cells(include_cell1);

	// Now remove cells from the exgrids and grid2s that interacted with grid1
	for (auto sheet=sheets.begin(); sheet != sheets.end(); ++sheet) {
		sheet->filter_cells1(include_cell1);
	}

}

std::unique_ptr<IceSheet> new_ice_sheet(Grid::Parameterization parameterization)
{
	switch(parameterization.index()) {
		case Grid::Parameterization::L0 : {
			IceSheet *ics = new IceSheet_L0;
			return std::unique_ptr<IceSheet>(ics);
//			return std::unique_ptr<IceSheet>(new IceSheet_L0);
		} break;
#if 0
		case Grid::Parameterization::L1 :
			return std::unique_ptr<IceSheet>(new IceSheet_L1);
		break;
#endif
		default :
			fprintf(stderr, "Unrecognized parameterization: %s\n", parameterization.str());
			throw std::exception();
	}
}


}
