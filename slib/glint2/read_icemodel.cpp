#include <glint2/IceModel.hpp>
#include <glint2/IceModel_DISMAL.hpp>

namespace glint2 {

/** @param sheet (OPTIONAL): Info on the ice sheet data structure */
std::unique_ptr<IceModel> read_icemodel(NcFile &nc, std::string const &vname, IceSheet const *sheet)
{
	printf("BEGIN read_icemodel(%s)\n", vname.c_str());
	auto info_var = nc.get_var((vname + ".info").c_str());

	IceModel::Type type = giss::parse_enum<IceModel::Type>(
		giss::get_att(info_var, "ice_model")->as_string(0));

	std::unique_ptr<IceModel> ice_model;
	switch(type.index()) {
		case IceModel::Type::DISMAL : {
			Grid_XY const *grid2 = dynamic_cast<Grid_XY const *>(&*(sheet->grid2));
			ice_model.reset(new IceModel_DISMAL(*grid2));
		} break;
#if 0
		case IceModel::Type::PISM :
			ice_model.reset(new IceModel_PISM());
			break;
		case IceModel::Type::ISSM :
			ice_model.reset(new IceModel_ISSM());
			break;
#endif
	}

	ice_model->read_from_netcdf(nc, vname);
	return ice_model;
	printf("END read_icemodel(%s)\n", vname.c_str());
}
// -----------------------------------------------------

}