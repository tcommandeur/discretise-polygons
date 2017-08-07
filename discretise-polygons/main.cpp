#include <ogrsf_frmts.h>
#include <iostream>

int main(int argc, const char * argv[]) {
  std::string filename = "";
  std::string ofname = "";
  bool filterHoogte = true;
  int maxangle = 10;
  if (argc == 4 || argc == 5) {
    filename = argv[1];
    ofname = argv[2];
    if (strcmp(argv[3], "false") == 0) {
      filterHoogte = false;
    }
    if (argc == 5) {
      maxangle = std::stoi(argv[4]);
    }
  }
  else {
    std::cerr << "Usage is wrong, inputfile outputfile removeheigts maxangle\n";
    return 0;
  }

  if (GDALGetDriverCount() == 0)
    GDALAllRegister();

  GDALDataset *dataSource = (GDALDataset*)GDALOpenEx(filename.c_str(), GDAL_OF_READONLY, NULL, NULL, NULL);
  OGRLayer *dataLayer = dataSource->GetLayer(0);
  OGRwkbGeometryType layerType = wkbFlatten(dataLayer->GetGeomType());

  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("gpkg");
  GDALDataset *oSource = driver->Create(ofname.c_str(), 0, 0, 0, GDT_Unknown, NULL);
  size_t lastindex = ofname.find_last_of(".");
  std::string rawname = ofname.substr(0, lastindex);
  OGRSpatialReference* sr = new OGRSpatialReference();
  sr->importFromEPSG(28992);
  OGRwkbGeometryType oLayerType = wkbPolygon;
  if (layerType == wkbMultiSurface) {
    oLayerType = wkbMultiPolygon;
  }
  OGRLayer *oLayer = oSource->CreateLayer(rawname.c_str(), sr, oLayerType, NULL);
  OGRFeatureDefn *iLayerDefn = dataLayer->GetLayerDefn();
  for (int i = 0; i < iLayerDefn->GetFieldCount(); i++) {
    oLayer->CreateField(iLayerDefn->GetFieldDefn(i));
  }

  OGRFeature *f;
  int count = 0;
  int readcount = 0;
  char *options[255] = {"ADD_INTERMEDIATE_POINT=NO"};
  while ((f = dataLayer->GetNextFeature()) != NULL) {
    readcount++;
    int eindrIdx = f->GetFieldIndex("eindRegistratie");
    if ((eindrIdx == -1 || !f->IsFieldSet(eindrIdx)) && (filterHoogte || f->GetFieldAsInteger("relatieveHoogteligging") == 0)) {
      OGRGeometry *geometry = f->GetGeometryRef();
      if (!geometry->IsValid()) {
        std::cerr << "Geometry invalid: " << f->GetFID() << std::endl;
      }
      //char** wkt = new char*;
      //geometry->exportToWkt(wkt);
      //printf("%s\n", *wkt);

      OGRwkbGeometryType eGeomType = wkbFlatten(geometry->getGeometryType());
      if (eGeomType == wkbCurvePolygon || eGeomType == wkbPolygon) {
        OGRCurvePolygon *curved = dynamic_cast<OGRCurvePolygon *>(geometry);
        if (curved == NULL) {
          std::cerr << "dynamic_cast failed. Expected OGRCurvePolygon." << std::endl;
          return false;
        }

        OGRPolygon *poly = curved->CurvePolyToPoly(maxangle, options);
        //char** wkt = new char*;
        //poly->exportToWkt(wkt);
        //printf("%s\n", *wkt);
        OGRFeature *feature = OGRFeature::CreateFeature(oLayer->GetLayerDefn());
        feature->SetGeometry(poly);
        for (int i = 0; i < f->GetFieldCount(); i++) {
          feature->SetField(i, f->GetRawFieldRef(i));
        }
        oLayer->CreateFeature(feature);
        OGRFeature::DestroyFeature(feature);
        count++;
      }
      else if (eGeomType == wkbMultiSurface || eGeomType == wkbMultiPolygon) {
        OGRMultiSurface *multiSurface;
        multiSurface = dynamic_cast<OGRMultiSurface *>(geometry);
        if (multiSurface == NULL) {
          std::cerr << "dynamic_cast failed. Expected OGRMultiSurface." << std::endl;
          return false;
        }

        OGRPolygon *poly = new OGRPolygon();
        OGRMultiPolygon *multiPoly = new OGRMultiPolygon();
        int numGeom = multiSurface->getNumGeometries();

        if (numGeom == 1) {
          OGRCurvePolygon *curved = dynamic_cast<OGRCurvePolygon *>(multiSurface->getGeometryRef(0));
          if (curved == NULL) {
            std::cerr << "dynamic_cast failed. Expected OGRCurvePolygon." << std::endl;
            return false;
          }

          poly = curved->CurvePolyToPoly(maxangle, options);
          //char** wkt = new char*;
          //poly->exportToWkt(wkt);
          //printf("%s\n", *wkt);
        }
        else {
          for (int i = 0; i < numGeom; i++) {
            OGRCurvePolygon *curveGeom = dynamic_cast<OGRCurvePolygon *>(multiSurface->getGeometryRef(i));
            if (curveGeom == NULL) {
              std::cerr << "dynamic_cast failed. Expected OGRCurvePolygon." << std::endl;
              return false;
            }

            OGRPolygon *tmpPoly = curveGeom->CurvePolyToPoly(maxangle, options);
            //char** wkt = new char*;
            //tmpPoly->exportToWkt(wkt);
            //printf("%s\n", *wkt);

            OGRErr err = multiPoly->addGeometry(tmpPoly);
            if (err != 0) {
              std::cerr << "GDAL Error: " << err << " occured, exiting.." << std::endl;
              return false;
            }
          }
        }

        OGRFeature *feature = OGRFeature::CreateFeature(oLayer->GetLayerDefn());
        if (numGeom == 1) {
          feature->SetGeometry(poly);
        }
        else {
          feature->SetGeometry(multiPoly);
        }
        for (int i = 0; i < f->GetFieldCount(); i++) {
          feature->SetField(i, f->GetRawFieldRef(i));
        }
        oLayer->CreateFeature(feature);
        OGRFeature::DestroyFeature(feature);
        count++;
      }
      else {
        std::cerr << "Geometry is not a Polygon, skipping " << eGeomType << std::endl;
      }
    }
  }
  dataSource->Release();
}