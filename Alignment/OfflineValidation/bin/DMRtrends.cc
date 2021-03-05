#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <iomanip>
#include <fstream>
#include <experimental/filesystem>
#include "TPad.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TGraphErrors.h"
#include "TMultiGraph.h"
#include "TH1.h"
#include "THStack.h"
#include "TROOT.h"
#include "TFile.h"
#include "TColor.h"
#include "TLegend.h"
#include "TLegendEntry.h"
#include "TMath.h"
#include "TRegexp.h"
#include "TPaveLabel.h"
#include "TPaveText.h"
#include "TStyle.h"
#include "TLine.h"
#include "Alignment/OfflineValidation/plugins/ColorParser.C"

#include <Alignment/OfflineValidation/bin/Options.h>
#include <Alignment/OfflineValidation/bin/exceptions.h>

#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/optional.hpp>

using namespace std;
namespace pt = boost::property_tree;
namespace fs = std::experimental::filesystem;
using namespace AllInOneConfig;

std::vector<std::string> getKeys(pt::ptree& tree){
    std::vector<std::string> keys;

    for(std::pair<std::string, pt::ptree> childTree : tree){
        keys.push_back(childTree.first);
    }

    return keys;
}

template <typename T>
std::vector<T> getVector(pt::ptree& tree, const std::string& name){
    std::vector<T> out;

    for(std::pair<std::string, pt::ptree> childTree : tree.get_child(name)){
        out.push_back(childTree.second.get_value<T>());
    }

    return out;
}


/*!
 * \def Dummy value in case a DMR would fail for instance
 */
#define DUMMY -999.
/*!
 * \def Scale factor value to have luminosity expressed in fb^-1
 */
#define lumiFactor 1000.
/*!
 * \def Scale factor value to have mean and sigmas expressed in micrometers.
 */
#define DMRFactor 10000.
/*!
 * \def First run for each year. TODO: runs taken from the lumi-per-run files, find a better definition!
 */
#define FIRSTRUN2016 272008
#define FIRSTRUN2017 294927
#define FIRSTRUN2018 315257

/*! \struct Point
 *  \brief Structure Point
 *         Contains parameters of Gaussian fits to DMRs
 *  
 * @param run:             run number (IOV boundary)
 * @param scale:           scale for the measured quantity: cm->μm for DMRs, 1 for normalized residuals
 * @param mu:              mu/mean from Gaussian fit to DMR/DrmsNR
 * @param sigma:           sigma/standard deviation from Gaussian fit to DMR/DrmsNR
 * @param muplus:          mu/mean for the inward pointing modules
 * @param muminus:         mu/mean for outward pointing modules
 * @param sigmaplus:       sigma/standard for inward pointing modules 
 * @param sigmaminus: //!< sigma/standard for outward pointing modules
 */
struct Point {
  float run, scale, mu, sigma, muplus, muminus, sigmaplus, sigmaminus;

  /*! \fn Point
     *  \brief Constructor of structure Point, initialising all members one by one
     */
  Point(float Run = DUMMY,
        float ScaleFactor = DMRFactor,
        float y1 = DUMMY,
        float y2 = DUMMY,
        float y3 = DUMMY,
        float y4 = DUMMY,
        float y5 = DUMMY,
        float y6 = DUMMY)
      : run(Run), scale(ScaleFactor), mu(y1), sigma(y2), muplus(y3), muminus(y5), sigmaplus(y4), sigmaminus(y6) {}

  /*! \fn Point
     *  \brief Constructor of structure Point, initialising all members from DMRs directly (with split)
     */
  Point(float Run, float ScaleFactor, TH1 *histo, TH1 *histoplus, TH1 *histominus)
      : Point(Run,
              ScaleFactor,
              histo->GetMean(),
              histo->GetMeanError(),
              histoplus->GetMean(),
              histoplus->GetMeanError(),
              histominus->GetMean(),
              histominus->GetMeanError()) {}

  /*! \fn Point
     *  \brief Constructor of structure Point, initialising all members from DMRs directly (without split)
     */
  Point(float Run, float ScaleFactor, TH1 *histo) : Point(Run, ScaleFactor, histo->GetMean(), histo->GetMeanError()) {}

  Point &operator=(const Point &p) {
    run = p.run;
    mu = p.mu;
    muplus = p.muplus;
    muminus = p.muminus;
    sigma = p.sigma;
    sigmaplus = p.sigmaplus;
    sigmaminus = p.sigmaminus;
    return *this;
  }

  float GetRun() const { return run; }
  float GetMu() const { return scale * mu; }
  float GetMuPlus() const { return scale * muplus; }
  float GetMuMinus() const { return scale * muminus; }
  float GetSigma() const { return scale * sigma; }
  float GetSigmaPlus() const { return scale * sigmaplus; }
  float GetSigmaMinus() const { return scale * sigmaminus; }
  float GetDeltaMu() const {
    if (muplus == DUMMY && muminus == DUMMY)
      return DUMMY;
    else
      return scale * (muplus - muminus);
  }
  float GetSigmaDeltaMu() const {
    if (sigmaplus == DUMMY && sigmaminus == DUMMY)
      return DUMMY;
    else
      return scale * hypot(sigmaplus, sigmaminus);
  };
};

///**************************
///*  Function declaration  *
///**************************

TString getName(TString structure, int layer, TString geometry);
TH1F *ConvertToHist(TGraphErrors *g);
const map<TString, int> numberOfLayers(TString Year = "2018");
vector<int> runlistfromlumifile(TString lumifile = "/afs/cern.ch/work/a/acardini/Alignment/MultiIOV/CMSSW_10_5_0_pre2/src/Alignment/OfflineValidation/data/lumiperFullRun2.txt");
bool checkrunlist(vector<int> runs, vector<int> IOVlist = {});
TString lumifileperyear(TString Year = "2018", string RunOrIOV = "IOV");
void scalebylumi(TGraphErrors *g, vector<pair<int, double>> lumiIOVpairs);
vector<pair<int, double>> lumiperIOV(vector<int> IOVlist, TString Year = "2018");
double getintegratedlumiuptorun(int run, TString lumifile, double min = 0.);
void PixelUpdateLines(TCanvas *c,
                      TString lumifile = "/afs/cern.ch/work/a/acardini/Alignment/MultiIOV/CMSSW_10_5_0_pre2/src/Alignment/OfflineValidation/data/lumiperFullRun2.txt",
                      bool showlumi = false,
                      vector<int> pixelupdateruns = {314881, 316758, 317527, 318228, 320377});
void PlotDMRTrends(
    TString Variable = "median",
    TString Year = "2018",
    vector<string> geometries = {"GT", "SG", "MP pix LBL", "PIX HLS+ML STR fix"},
    vector<Color_t> colours = {kBlue, kRed, kGreen, kCyan},
    TString outputdir =
        "/afs/cern.ch/cms/CAF/CMSALCA/ALCA_TRACKERALIGN/data/commonValidation/alignmentObjects/acardini/DMRsTrends/",
    vector<int> pixelupdateruns = {314881, 316758, 317527, 318228, 320377},
    bool showlumi = false,
    TString lumifile = "/afs/cern.ch/work/a/acardini/Alignment/MultiIOV/CMSSW_10_5_0_pre2/src/Alignment/OfflineValidation/data/lumiperFullRun2.txt",
    vector<pair<int,double>> lumiIOVpairs = {make_pair(0,0.),make_pair(0,0.)});
void compileDMRTrends(
    vector<int> IOVlist,
    TString Variable = "median",
    TString Year = "2018",
    std::vector<std::string> inputFiles = {},
    TString outDir = "",
    vector<string> geometries = {"GT", "SG", "MP pix LBL", "PIX HLS+ML STR fix"},
    bool showlumi = false,
    bool FORCE = false);
void DMRtrends(
    vector<int> IOVlist,
    vector<string> Variables = {"median", "DrmsNR"},
    TString Year = "2018",
    std::vector<std::string> inputFiles = {},
    vector<string> geometries = {"GT", "SG", "MP pix LBL", "PIX HLS+ML STR fix"},
    vector<Color_t> colours = {kBlue, kRed, kGreen, kCyan},
    TString outputdir =
        "/afs/cern.ch/cms/CAF/CMSALCA/ALCA_TRACKERALIGN/data/commonValidation/alignmentObjects/acardini/DMRsTrends/",
    vector<int> pixelupdateruns = {314881, 316758, 317527, 318228, 320377},
    bool showlumi = false,
    TString lumifile = "/afs/cern.ch/work/a/acardini/Alignment/MultiIOV/CMSSW_10_5_0_pre2/src/Alignment/OfflineValidation/data/lumiperFullRun2.txt",
    bool FORCE = false);

/*! \class Geometry
 *  \brief Class Geometry
 *         Contains vector for fit parameters (mean, sigma, etc.) obtained from multiple IOVs
 *         See Structure Point for description of the parameters.
 */

class Geometry {
public:
  vector<Point> points;

private:
  //template<typename T> vector<T> GetQuantity (T (Point::*getter)() const) const {
  vector<float> GetQuantity(float (Point::*getter)() const) const {
    vector<float> v;
    for (Point point : points) {
      float value = (point.*getter)();
      v.push_back(value);
    }
    return v;
  }

public:
  TString title;
  Geometry() : title("") {}
  Geometry(TString Title) : title(Title) {}
  Geometry &operator=(const Geometry &geom) {
    title = geom.title;
    points = geom.points;
    return *this;
  }
  void SetTitle(TString Title) { title = Title; }
  TString GetTitle() { return title; }
  vector<float> Run() const { return GetQuantity(&Point::GetRun); }
  vector<float> Mu() const { return GetQuantity(&Point::GetMu); }
  vector<float> MuPlus() const { return GetQuantity(&Point::GetMuPlus); }
  vector<float> MuMinus() const { return GetQuantity(&Point::GetMuMinus); }
  vector<float> Sigma() const { return GetQuantity(&Point::GetSigma); }
  vector<float> SigmaPlus() const { return GetQuantity(&Point::GetSigmaPlus); }
  vector<float> SigmaMinus() const { return GetQuantity(&Point::GetSigmaMinus); }
  vector<float> DeltaMu() const { return GetQuantity(&Point::GetDeltaMu); }
  vector<float> SigmaDeltaMu() const { return GetQuantity(&Point::GetSigmaDeltaMu); }
  //vector<float> Graph (string variable) const {
  // };
};

/// DEPRECATED
//struct Layer {
//    map<string,Geometry> geometries;
//};
//
//struct HLS {
//    vector<Layer> layers;
//    map<string,Geometry> geometries;
//};

/*! \fn getName
 *  \brief Function used to get a string containing information on the high level structure, the layer/disc and the geometry.
 */

TString getName(TString structure, int layer, TString geometry) {
  geometry.ReplaceAll(" ", "_");
  TString name = geometry + "_" + structure;
  if (layer != 0) {
    if (structure == "TID" || structure == "TEC")
      name += "_disc";
    else
      name += "_layer";
    name += layer;
  }

  return name;
};

/*! \fn numberOfLayers
 *  \brief Function used to retrieve a map containing the number of layers per subdetector
 */

const map<TString, int> numberOfLayers(TString Year) {
  if (Year == "2016")
    return {{"BPIX", 3}, {"FPIX", 2}, {"TIB", 4}, {"TID", 3}, {"TOB", 6}, {"TEC", 9}};
  else
    return {{"BPIX", 4}, {"FPIX", 3}, {"TIB", 4}, {"TID", 3}, {"TOB", 6}, {"TEC", 9}};
}

/// DEPRECATED
/*! \fn lumifileperyear
 *  \brief Function to retrieve the file with luminosity per run/IOV
 *         The use of a lumi-per-IOV file is deprecated, but can still be useful for debugging
 */

TString lumifileperyear(TString Year, string RunOrIOV) {
  TString LumiFile = std::getenv("CMSSW_BASE");
  LumiFile += "/src/Alignment/OfflineValidation/data/lumiper";
  if (RunOrIOV != "run" && RunOrIOV != "IOV") {
    cout << "ERROR: Please specify \"run\" or \"IOV\" to retrieve the luminosity run by run or for each IOV" << endl;
    exit(EXIT_FAILURE);
  }
  LumiFile += RunOrIOV;
  if (Year != "2016" && Year != "2017" && Year != "2018") {
    cout << "ERROR: Only 2016, 2017 and 2018 lumi-per-run files are available, please check!" << endl;
    exit(EXIT_FAILURE);
  }
  LumiFile += Year;
  LumiFile += ".txt";
  return LumiFile;
};

/*! \fn runlistfromlumifile
 *  \brief Get a vector containing the list of runs for which the luminosity is known.
 */

vector<int> runlistfromlumifile(TString lumifile) {
  vector<pair<int, double>> lumiperRun;

  std::ifstream infile(lumifile.Data());
  int run;
  double runLumi;
  while (infile >> run >> runLumi) {
    lumiperRun.push_back(make_pair(run,runLumi));
  }
  size_t nRuns = lumiperRun.size();
  vector<int> xRunFromLumiFile;
  for (size_t iRun = 0; iRun < nRuns; iRun++)
    xRunFromLumiFile.push_back(lumiperRun[iRun].first);
  return xRunFromLumiFile;
}

/*! \fn checkrunlist
 *  \brief Check whether all runs of interest are present in the luminosity per run txt file and whether all IOVs analized have been correctly processed
 */

bool checkrunlist(vector<int> runs, vector<int> IOVlist) {
  vector<int> missingruns;  //runs for which the luminosity is not found
  vector<int> lostruns;     //IOVs for which the DMR were not found
  bool problemfound = false;
  std::sort(runs.begin(), runs.end());
  std::sort(IOVlist.begin(), IOVlist.end());
  if (!IOVlist.empty())
    for (auto IOV : IOVlist) {
      if (find(runs.begin(), runs.end(), IOV) == runs.end()) {
        problemfound = true;
        lostruns.push_back(IOV);
      }
    }
  if (problemfound) {
    if (!lostruns.empty()) {
      cout << "WARNING: some IOVs where not found among the list of available DMRs" << endl
           << "List of missing IOVs:" << endl;
      for (int lostrun : lostruns)
        cout << to_string(lostrun) << " ";
      cout << endl;
    }
  }
  return problemfound;
}

/*! \fn DMRtrends
 *  \brief Create and plot the DMR trends.
 */

void DMRtrends(vector<int> IOVlist,
               vector<string> Variables,
               TString Year,
               std::vector<std::string> inputFiles,
               vector<string> geometries,
               vector<Color_t> colours,
               TString outputdir,
               vector<int> pixelupdateruns,
               bool showlumi,
	       TString lumifile,
               bool FORCE) {
  fs::path path(outputdir.Data());
  if (!(fs::exists(path))) {
    cout << "WARNING: Output directory (" << outputdir.Data() << ") not found, it will be created automatically!"
         << endl;
    //	exit(EXIT_FAILURE);
    fs::create_directory(path);
    if (!(fs::exists(path))) {
      cout << "ERROR: Output directory (" << outputdir.Data() << ") has not been created!" << endl
           << "At least the parent directory needs to exist, please check!" << endl;
      exit(EXIT_FAILURE);
    }
  }
  
  TString LumiFile = getenv("CMSSW_BASE");
  if(lumifile.BeginsWith("/")) LumiFile=lumifile;
  else {
    LumiFile += "/src/Alignment/OfflineValidation/data/";
    LumiFile += lumifile;
  }
  fs::path pathToLumiFile = LumiFile.Data();
  if(!(fs::exists(pathToLumiFile))) {
    cout << "ERROR: lumi-per-run file (" << LumiFile.Data() << ") not found!" << endl
           << "Please check!" << endl;
    exit(EXIT_FAILURE); 
  }
  if(!LumiFile.Contains(Year)){
    cout << "WARNING: lumi-per-run file and year do not match, luminosity on the x-axis and labels might not match!" <<endl;
  }
  vector<pair<int,double>> lumiIOVpairs = lumiperIOV(IOVlist, LumiFile);

  for (TString Variable : Variables) {
    compileDMRTrends(IOVlist, Variable, Year, inputFiles, outputdir, geometries, showlumi, FORCE);
    cout << "Begin plotting" << endl;
    PlotDMRTrends(
                  Variable,
                  Year,
                  geometries,
                  colours,
                  outputdir,
                  pixelupdateruns,
                  showlumi,
		  LumiFile,
		  lumiIOVpairs);
  }
};

/*! \fn compileDMRTrends
 *  \brief  Create a file where the DMR trends are stored in the form of TGraph.
 */

void compileDMRTrends(vector<int> IOVlist,
                      TString Variable,
                      TString Year,
                      std::vector<std::string> inputFiles,
                      TString outDir,
                      vector<string> geometries,
                      bool showlumi,
                      bool FORCE) {
  gROOT->SetBatch();

  vector<TString> structures{"BPIX", "BPIX_y", "FPIX", "FPIX_y", "TIB", "TID", "TOB", "TEC"};

  const map<TString, int> nlayers = numberOfLayers(Year);

  float ScaleFactor = DMRFactor;
  if (Variable == "DrmsNR")
    ScaleFactor = 1;

  map<pair<pair<TString, int>, TString>, Geometry> mappoints;  // pair = (structure, layer), geometry

  for (unsigned int i = 0; i < inputFiles.size(); ++i) {
    int runN = IOVlist.at(i);

    TFile *f = new TFile(inputFiles.at(i).c_str(), "READ");

    std::cout << inputFiles.at(i) << std::endl;

    for (TString &structure : structures) {
      TString structname = structure;
      structname.ReplaceAll("_y", "");
      size_t layersnumber = nlayers.at(structname);
      for (size_t layer = 0; layer <= layersnumber; layer++) {
        for (string geometry : geometries) {
          TString name = Variable + "_" + getName(structure, layer, geometry);
          TH1F *histo = dynamic_cast<TH1F *>(f->Get(name));
          //Geometry *geom =nullptr;
          Point *point = nullptr;
          // Three possibilities:
          //  - All histograms are produced correctly
          //  - Only the non-split histograms are produced
          //  - No histogram is produced correctly
          //  FORCE means that the Point is not added to the points collection in the chosen geometry for that structure
          //  If FORCE is not enabled a default value for the Point is used (-9999) which will appear in the plots
          if (!histo) {
           // cout << "Run" << runN << " Histogram: " << name << " not found" << endl;
            if (FORCE)
              continue;
            point = new Point(runN, ScaleFactor);
          } else if (structure != "TID" && structure != "TEC") {
            TH1F *histoplus = dynamic_cast<TH1F *>(f->Get((name + "_plus")));
            TH1F *histominus = dynamic_cast<TH1F *>(f->Get((name + "_minus")));
            if (!histoplus || !histominus) {
               // cout << "Run" << runN << " Histogram: " << name << " plus or minus not found" << endl;
              if (FORCE)
                continue;
              point = new Point(runN, ScaleFactor, histo);
            } else
              point = new Point(runN, ScaleFactor, histo, histoplus, histominus);

          } else
            point = new Point(runN, ScaleFactor, histo);
          mappoints[make_pair(make_pair(structure, layer), geometry)].points.push_back(*point);
        }
      }
    }
    f->Close();
  }
  TString outname = outDir + "/trends.root";
  cout << outname << endl;
  TFile *fout = TFile::Open(outname, "RECREATE");
  for (TString &structure : structures) {
    TString structname = structure;
    structname.ReplaceAll("_y", "");
    size_t layersnumber = nlayers.at(structname);
    for (size_t layer = 0; layer <= layersnumber; layer++) {
      for (string geometry : geometries) {
        TString name = Variable + "_" + getName(structure, layer, geometry);
        Geometry geom = mappoints[make_pair(make_pair(structure, layer), geometry)];
        using Trend = vector<float> (Geometry::*)() const;
        vector<Trend> trends{&Geometry::Mu,
                             &Geometry::Sigma,
                             &Geometry::MuPlus,
                             &Geometry::SigmaPlus,
                             &Geometry::MuMinus,
                             &Geometry::SigmaMinus,
                             &Geometry::DeltaMu,
                             &Geometry::SigmaDeltaMu};
        vector<TString> variables{
            "mu", "sigma", "muplus", "sigmaplus", "muminus", "sigmaminus", "deltamu", "sigmadeltamu"};
        vector<float> runs = geom.Run();
        size_t n = runs.size();
        vector<float> emptyvec;
        for (size_t i = 0; i < runs.size(); i++)
          emptyvec.push_back(0.);
        for (size_t iVar = 0; iVar < variables.size(); iVar++) {
          Trend trend = trends.at(iVar);
          TGraphErrors *g = new TGraphErrors(n, runs.data(), (geom.*trend)().data(), emptyvec.data(), emptyvec.data());
          g->SetTitle(geometry.c_str());
          g->Write(name + "_" + variables.at(iVar));
        }
        vector<pair<Trend, Trend>> trendspair{make_pair(&Geometry::Mu, &Geometry::Sigma),
                                              make_pair(&Geometry::MuPlus, &Geometry::SigmaPlus),
                                              make_pair(&Geometry::MuMinus, &Geometry::SigmaMinus),
                                              make_pair(&Geometry::DeltaMu, &Geometry::SigmaDeltaMu)};
        vector<pair<TString, TString>> variablepairs{make_pair("mu", "sigma"),
                                                     make_pair("muplus", "sigmaplus"),
                                                     make_pair("muminus", "sigmaminus"),
                                                     make_pair("deltamu", "sigmadeltamu")};
        for (size_t iVar = 0; iVar < variablepairs.size(); iVar++) {
          Trend meantrend = trendspair.at(iVar).first;
          Trend sigmatrend = trendspair.at(iVar).second;
          TGraphErrors *g = new TGraphErrors(
              n, runs.data(), (geom.*meantrend)().data(), emptyvec.data(), (geom.*sigmatrend)().data());
          g->SetTitle(geometry.c_str());
          TString graphname = name + "_" + variablepairs.at(iVar).first;
          graphname += variablepairs.at(iVar).second;
          g->Write(graphname);
        }
      }
    }
  }
  fout->Close();
}


/*! \fn PixelUpdateLines
 *  \brief  Adds to the canvas vertical lines corresponding to the pixelupdateruns
 */
void PixelUpdateLines(TCanvas *c, TString lumifile, bool showlumi, vector<int> pixelupdateruns) {
  vector<TPaveText *> labels;
  double lastlumi = 0.;
  c->cd();
  size_t index = 0;
  //Due to the way the coordinates within the Canvas are set, the following steps are required to draw the TPaveText:
  // Compute the gPad coordinates in TRUE normalized space (NDC)
  int ix1;
  int ix2;
  int iw = gPad->GetWw();
  int ih = gPad->GetWh();
  double x1p, y1p, x2p, y2p;
  gPad->GetPadPar(x1p, y1p, x2p, y2p);
  ix1 = (Int_t)(iw * x1p);
  ix2 = (Int_t)(iw * x2p);
  double wndc = TMath::Min(1., (double)iw / (double)ih);
  double rw = wndc / (double)iw;
  double x1ndc = (double)ix1 * rw;
  double x2ndc = (double)ix2 * rw;
  // Ratios to convert user space in TRUE normalized space (NDC)
  double rx1, ry1, rx2, ry2;
  gPad->GetRange(rx1, ry1, rx2, ry2);
  double rx = (x2ndc - x1ndc) / (rx2 - rx1);
  int ola=0;

  for (int pixelupdaterun : pixelupdateruns) {
    double lumi = 0.;
  
 

    char YearsNames[5][5]={"2016", "2017", "2018"};


    if (showlumi)
      lumi = getintegratedlumiuptorun(
          pixelupdaterun,
          lumifile);  //The vertical line needs to be drawn at the beginning of the run where the pixel update was implemented, thus only the integrated luminosity up to that run is required.
    else
      lumi = pixelupdaterun;
  
// here to plot with one style runs from which pixel iov was otained 

//  below to plot with one style pixel uptade runs

    TLine *line = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());
 if (pixelupdaterun==271866|| pixelupdaterun==272008|| pixelupdaterun==276315|| pixelupdaterun==278271|| pixelupdaterun==280928|| pixelupdaterun==290543|| pixelupdaterun==294927|| pixelupdaterun== 297281|| pixelupdaterun==298653|| pixelupdaterun==299443|| pixelupdaterun==300389|| pixelupdaterun==301046|| pixelupdaterun==302131|| pixelupdaterun==303790|| pixelupdaterun==303998|| pixelupdaterun==304911|| pixelupdaterun==313041|| pixelupdaterun==314881|| pixelupdaterun==315257|| pixelupdaterun==316758|| pixelupdaterun==317475|| pixelupdaterun==317485|| pixelupdaterun==317527|| pixelupdaterun==317661|| pixelupdaterun==317664|| pixelupdaterun==318227|| pixelupdaterun==320377|| pixelupdaterun==321831|| pixelupdaterun==322510|| pixelupdaterun==322603|| pixelupdaterun==323232|| pixelupdaterun==324245)
{
    line->SetLineColor(kBlack);
    line->SetLineStyle(3); // it was 9, For public plots changed to 3
    line->Draw();
} 

//  324245 those iovs are in both lists so I need to drwa both on top of each other
//the problem is around these lines: the IOVs so small that they overlap
//298653, 299061, 299443,
//318887, 320377, 320674,





    double _sx;
    // Left limit of the TPaveText

    _sx = rx * (lumi - rx1) + x1ndc;
    // To avoid an overlap between the TPaveText a vertical shift is done when the IOVs are too close
    if (_sx < lastlumi) {
  //    index++;     // ola: I commented it out becaouse if I plot only the names of years I dint need to change the position of the label
    } else
      index = 0;
   
    // FirstRunOftheYear 272008,294927,315257  
    //// first run of 2018 and run from which pixel template was obtained 314527 is very close 
// also first run of 2016 and 272022 are very close

    if (pixelupdaterun == 272008 || pixelupdaterun == 294927 || pixelupdaterun == 315257 || pixelupdaterun==314527 || pixelupdaterun ==272022)  
    {
    TPaveText *box = new TPaveText(_sx + 0.0028, 0.865 - index * 0.035, _sx + 0.055, 0.895 - index * 0.035, "blNDC"); 
   if (pixelupdaterun == 272008 || pixelupdaterun == 294927 || pixelupdaterun == 315257 ){

    box->SetFillColor(10);
    box->SetBorderSize(1);
    box->SetLineColor(kBlack);


  TText *textFirstRunOfYear = box->AddText(Form("%s", YearsNames[int(ola)])); // Ola for public plots
  //  TText *textFirstRunOfYear = box->AddText(Form("%i", int(pixelupdaterun))); 
    textFirstRunOfYear->SetTextSize(0.025); 
    labels.push_back(box);
     ola=ola+1;

}

if (pixelupdaterun == 294927 ){
    line->SetLineColor(kBlack);
    line->SetLineStyle(1);
    line->SetLineWidth(4);
    line->Draw();}
   
if (pixelupdaterun == 315257 || pixelupdaterun==314527 ){
//if (pixelupdaterun == 315257 ){
    line->SetLineColor(kBlack);
    line->SetLineStyle(1);
    line->SetLineWidth(4);
    line->Draw();
    TLine *line2 = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());

   // line2->SetLineColor(kYellow+1);  //it is problematic because it should be the same and it is not
    line2->SetLineColor(kRed+1);
    line2->SetLineStyle(1); // 
    line2->Draw();

 }

// the yellow line has to be thicker here to be not exactly at 0 and visible from below the y axis... 

 if (pixelupdaterun == 272008 || pixelupdaterun ==272022){
    line->SetLineColor(kBlack);
    line->SetLineStyle(1);
    line->SetLineWidth(4);
   // line->Draw();
    TLine *line2 = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());

    line2->SetLineColor(kYellow+1);
    line2->SetLineStyle(1); // 
    line->SetLineWidth(10);
    line2->Draw();

 }

    }


    TLine *line11 = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());
if (pixelupdaterun==272022|| pixelupdaterun==276811|| pixelupdaterun==279767|| pixelupdaterun==283308|| pixelupdaterun==297057|| pixelupdaterun==297503||
 pixelupdaterun==299061|| 
pixelupdaterun==300157||
 pixelupdaterun==300401|| pixelupdaterun==301183|| pixelupdaterun==302472|| pixelupdaterun==303885|| pixelupdaterun==304292|| pixelupdaterun==305898|| pixelupdaterun==314527|| pixelupdaterun==316766|| pixelupdaterun==317484|| pixelupdaterun==317641|| pixelupdaterun==318887|| pixelupdaterun==320674|| pixelupdaterun==321833|| pixelupdaterun==324245)
{
//315257

    line11->SetLineColor(kYellow+1);
    line11->SetLineStyle(1); // 
    line11->Draw();

}
    
 if (pixelupdaterun == 298653 || pixelupdaterun ==  299061 || pixelupdaterun ==  320377 || pixelupdaterun ==  320674 || pixelupdaterun == 324245 )
{

TLine *line2 = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());

    line2->SetLineColor(kYellow+1);
    line2->SetLineStyle(1); // 
    line2->Draw();

    line->SetLineColor(kBlack);
    line->SetLineStyle(3); // it was 9, For public plots changed to 3
    line->Draw();

}

 TLine *line33 = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());
 //if (pixelupdaterun == 298653 || pixelupdaterun ==  299061 || pixelupdaterun ==  320377 || pixelupdaterun ==  320674){
 if (pixelupdaterun ==299061 || pixelupdaterun == 301183 || pixelupdaterun == 304292 || pixelupdaterun == 314527 || pixelupdaterun == 317484 || pixelupdaterun == 317641 || pixelupdaterun == 320674 || pixelupdaterun == 321833 ) { 
   
   line33->SetLineColor(kRed+1);
   line33->SetLineWidth(1);
   line33->SetLineStyle(1); 
   line33->Draw();

   if (pixelupdaterun == 299061 || pixelupdaterun == 317484 || pixelupdaterun == 320674 ){
     TLine *line333 = new TLine(lumi, c->GetUymin(), lumi, c->GetUymax());
     line333->SetLineColor(kBlack);
     line333->SetLineWidth(1);
     line333->SetLineStyle(3); 
     line333->Draw();
   }
 }
 
 lastlumi = _sx + 0.075; 
 
 
 //  gPad->RedrawAxis();
  }
  //Drawing in a separate loop to ensure that the labels are drawn on top of the lines
  for (auto label : labels) {
    label->Draw("same");
  }
  c->Update();
}


/*! \fn getintegratedlumiuptorun
 *  \brief Returns the integrated luminosity up to the run of interest
 *         Use -1 to get integrated luminosity over the whole period
 */

double getintegratedlumiuptorun(int ChosenRun, TString lumifile, double min) {
  double lumi = min;
  vector<pair<int, double>> lumiperRun;

  std::ifstream infile(lumifile.Data());
  int run;
  double runLumi;
  while (infile >> run >> runLumi) {
    lumiperRun.push_back(make_pair(run,runLumi));
  }
  std::sort(lumiperRun.begin(),lumiperRun.end());
  for(size_t iRun=0; iRun<lumiperRun.size();iRun++){
    if(ChosenRun<=lumiperRun.at(iRun).first) break;
    lumi += (lumiperRun.at(iRun).second / lumiFactor);
  }
  return lumi;
}
/*! \fn scalebylumi
 *  \brief Scale X-axis of the TGraph and the error on that axis according to the integrated luminosity.
 */

void scalebylumi(TGraphErrors *g, vector<pair<int, double>> lumiIOVpairs) {
  size_t N = g->GetN();
  vector<double> x, y, xerr, yerr;

  //TGraph * scale = new TGraph((lumifileperyear(Year,"IOV")).Data());
  size_t Nscale = lumiIOVpairs.size();

  size_t i = 0;
  while (i < N) {
    double run, yvalue;
    g->GetPoint(i, run, yvalue);
    size_t index = -1;
    for (size_t j = 0; j < Nscale; j++) {
      if (run == (lumiIOVpairs.at(j).first)) {  //If the starting run of an IOV is included in the list of IOVs, the index is stored
        index = j;
        continue;
      } else if (run > (lumiIOVpairs.at(j).first))
        continue;
    }
    if (lumiIOVpairs.at(index).second == 0 || index < 0.) {
      N = N - 1;
      g->RemovePoint(i);
    } else {
      double xvalue = 0.;
      for (size_t j = 0; j < index; j++)
        xvalue += lumiIOVpairs.at(j).second / lumiFactor;
      x.push_back(xvalue + (lumiIOVpairs.at(index).second / (lumiFactor * 2.)));
      if (yvalue <= DUMMY) {
        y.push_back(DUMMY);
        yerr.push_back(0.);
      } else {
        y.push_back(yvalue);
        yerr.push_back(g->GetErrorY(i));
      }
      xerr.push_back(lumiIOVpairs.at(index).second / (lumiFactor * 2.));
      i = i + 1;
    }
  }
  g->GetHistogram()->Delete();
  g->SetHistogram(nullptr);
  for (size_t i = 0; i < N; i++) {
    g->SetPoint(i, x.at(i), y.at(i));
    g->SetPointError(i, xerr.at(i), yerr.at(i));
  }
}

/*! \fn lumiperIOV
 *  \brief Retrieve luminosity per IOV
 */

vector<pair<int, double>> lumiperIOV(vector<int> IOVlist, TString lumifile) {
  size_t nIOVs = IOVlist.size();
  vector<pair<int, double>> lumiperIOV;
  vector<pair<int, double>> lumiperRun;

  std::ifstream infile(lumifile.Data());
  int run;
  double runLumi;
  while (infile >> run >> runLumi) {
    lumiperRun.push_back(make_pair(run,runLumi));
  }
  vector<int> xRunFromLumiFile;
  vector<int> missingruns;
  for (size_t iRun = 0; iRun < lumiperRun.size(); iRun++)
    xRunFromLumiFile.push_back(lumiperRun[iRun].first);
  for (int run : IOVlist) {
    if (find(xRunFromLumiFile.begin(), xRunFromLumiFile.end(), run) == xRunFromLumiFile.end()) {
      missingruns.push_back(run);
      lumiperRun.push_back(make_pair(run,0.));
    }
  }
  std::sort(lumiperRun.begin(),lumiperRun.end());

  if (!missingruns.empty()) {
    cout << "WARNING: some IOVs are missing in the run/luminosity txt file: " << lumifile << endl
	 << "List of missing runs:" << endl;
    for (int missingrun : missingruns)
      cout << to_string(missingrun) << " ";
    cout << endl;
    cout << "NOTE: the missing runs are added in the code with luminosity = 0 (they are not stored in the input file), please check that the IOV numbers are correct!" << endl;
  }

  size_t i = 0;
  size_t index = 0;
  double lumiInput = 0.;
  double lumiOutput = 0.;

  // WIP

  while (i <= nIOVs) {
    double run = 0;
    double lumi = 0.;
    if (i != nIOVs)
      run = IOVlist.at(i);
    else
      run = 0;
    for (size_t j = index; j < lumiperRun.size(); j++) {
      //cout << run << " - " << lumiperRun.at(j).first << " - lumi added: " << lumi << endl;
      if (run == lumiperRun.at(j).first) {
	index = j;
	break;
      } else
	lumi += lumiperRun.at(j).second;
    }
    if (i == 0)
      lumiperIOV.push_back(make_pair(0, lumi));
      else
	lumiperIOV.push_back(make_pair(IOVlist.at(i - 1), lumi));
    ++i;
  }
  for (size_t j = 0; j < lumiperRun.size(); j++)
    lumiInput += lumiperRun.at(j).second;
  for (size_t j = 0; j < lumiperIOV.size(); j++)
    lumiOutput += lumiperIOV.at(j).second;
  if (abs(lumiInput - lumiOutput) > 0.5) {
    cout << "ERROR: luminosity retrieved for IOVs does not match the one for the runs." << endl
	 << "Please check that all IOV first runs are part of the run-per-lumi file!" << endl;
    cout << "Total lumi from lumi-per-run file: " << lumiInput <<endl;
    cout << "Total lumi saved for IOVs: " << lumiOutput <<endl;
    cout << "Size of IOVlist " << IOVlist.size() << endl;
    cout << "Size of lumi-per-IOV list " << lumiperIOV.size() << endl;
    //for (size_t j = 0; j < lumiperIOV.size(); j++)
    //  cout << (j==0 ? 0 : IOVlist.at(j-1)) << " == " << lumiperIOV.at(j).first << " " <<lumiperIOV.at(j).second <<endl;
    //for (auto pair : lumiperIOV) cout << pair.first << " ";
    //cout << endl;
    //for (auto IOV : IOVlist) cout << IOV << " ";
    //cout << endl;
    exit(EXIT_FAILURE);

  }
  cout << "final lumi= "<< lumiOutput<<endl;
  //for debugging
  //for (size_t j = 0; j < lumiperIOV.size(); j++)
  //  cout << lumiperIOV.at(j).first << " " <<lumiperIOV.at(j).second <<endl;
    
  return lumiperIOV;
}

/*! \fn ConvertToHist
 *  \brief A TH1F is constructed using the points and the errors collected in the TGraphErrors
 */

TH1F *ConvertToHist(TGraphErrors *g) {
  size_t N = g->GetN();
  double *x = g->GetX();
  double *y = g->GetY();
  double *xerr = g->GetEX();
  
  vector<float> bins;
  bins.push_back(x[0] - xerr[0]);
  for (size_t i = 1; i < N; i++) {
    if ((x[i - 1] + xerr[i - 1]) > (bins.back() + pow(10, -6)))
      bins.push_back(x[i - 1] + xerr[i - 1]);
    if ((x[i] - xerr[i]) > (bins.back() + pow(10, -6)))
      bins.push_back(x[i] - xerr[i]);
  }
  bins.push_back(x[N - 1] + xerr[N - 1]);
  TString histoname = "histo_";
  histoname += g->GetName();
  TH1F *histo = new TH1F(histoname, g->GetTitle(), bins.size() - 1, bins.data());
  for (size_t i = 0; i < N; i++) {
    histo->Fill(x[i], y[i]);
    histo->SetBinError(histo->FindBin(x[i]), pow(10, -6));
  }
  return histo;
}

/*! \fn PlotDMRTrends
 *  \brief Plot the DMR trends.
 */

void PlotDMRTrends(
                   TString Variable,
                   TString Year,
                   vector<string> geometries,
                   vector<Color_t> colours,
                   TString outputdir,
                   vector<int> pixelupdateruns,
                   bool showlumi,
                    TString lumifile,
                    vector<pair<int, double>> lumiIOVpairs) {
  gErrorIgnoreLevel = kWarning;
  checkrunlist(pixelupdateruns, {});
  vector<TString> structures{"BPIX", "BPIX_y", "FPIX", "FPIX_y", "TIB", "TID", "TOB", "TEC"};

  const map<TString, int> nlayers = numberOfLayers(Year);
  
  TString outname = outputdir + "/trends.root";

  TFile *in = new TFile(outname);
  for (TString &structure : structures) {
    TString structname = structure;
    structname.ReplaceAll("_y", "");
    int layersnumber = nlayers.at(structname);
    for (int layer = 0; layer <= layersnumber; layer++) {
      vector<TString> variables{"mu",
                                "sigma",
                                "muplus",
                                "sigmaplus",
                                "muminus",
                                "sigmaminus",
                                "deltamu",
                                "sigmadeltamu",
                                "musigma",
                                "muplussigmaplus",
                                "muminussigmaminus",
                                "deltamusigmadeltamu"};
      vector<string> YaxisNames{
          "#mu [#mum]",
          "#sigma_{#mu} [#mum]",
          "#mu outward [#mum]",
          "#sigma_{#mu outward} [#mum]",
          "#mu inward [#mum]",
          "#sigma_{#mu inward} [#mum]",
          "#Delta#mu [#mum]",
          "#sigma_{#Delta#mu} [#mum]",
          "#mu [#mum]",
          "#mu outward [#mum]",
          "#mu inward [#mum]",
          "#Delta#mu [#mum]",
      };
      if (Variable == "DrmsNR")
        YaxisNames = {
            "RMS(x'_{pred}-x'_{hit} /#sigma)",
            "#sigma_{RMS(x'_{pred}-x'_{hit} /#sigma)}",
            "RMS(x'_{pred}-x'_{hit} /#sigma) outward",
            "#sigma_{#mu outward}",
            "RMS(x'_{pred}-x'_{hit} /#sigma) inward",
            "#sigma_{RMS(x'_{pred}-x'_{hit} /#sigma) inward}",
            "#DeltaRMS(x'_{pred}-x'_{hit} /#sigma)",
            "#sigma_{#DeltaRMS(x'_{pred}-x'_{hit} /#sigma)}",
            "RMS(x'_{pred}-x'_{hit} /#sigma)",
            "RMS(x'_{pred}-x'_{hit} /#sigma) outward",
            "RMS(x'_{pred}-x'_{hit} /#sigma) inward",
            "#DeltaRMS(x'_{pred}-x'_{hit} /#sigma)",
        };
      //For debugging purposes we still might want to have a look at plots for a variable without errors, once ready for the PR those variables will be removed and the iterator will start from 0
      for (size_t i = 0; i < variables.size(); i++) {
        TString variable = variables.at(i);
        if (variable.Contains("plus") || variable.Contains("minus") || variable.Contains("delta")) {
          if (structname == "TEC" || structname == "TID")
            continue;  //Lorentz drift cannot appear in TEC and TID. These structures are skipped when looking at outward and inward pointing modules.
        }
        TCanvas *c = new TCanvas("dummy", "", 2000, 800);

        vector<Color_t>::iterator colour = colours.begin();

        TMultiGraph *mg = new TMultiGraph(structure, structure);
        THStack *mh = new THStack(structure, structure);
        size_t igeom = 0;
        for (string geometry : geometries) {
          TString name = Variable + "_" + getName(structure, layer, geometry);
          std::cout << name + "_" + variables.at(i) << std::endl;
          
          TGraphErrors *g = dynamic_cast<TGraphErrors *>(in->Get(name + "_" + variables.at(i)));
          g->Print();
          g->SetName(name + "_" + variables.at(i));
          if (i >= 8) {
            g->SetLineWidth(1);
            g->SetLineColor(*colour);
            g->SetFillColorAlpha(*colour, 0.2);
          }
          vector<vector<double>> vectors;
          //if(showlumi&&i<8)scalebylumi(dynamic_cast<TGraph*>(g));
          if (showlumi)
            scalebylumi(g, lumiIOVpairs);
          g->SetLineColor(*colour);
          g->SetMarkerColor(*colour);
          TH1F *h = ConvertToHist(g);
          h->SetLineColor(*colour);
          h->SetMarkerColor(*colour);
          h->SetMarkerSize(0);
          h->SetLineWidth(1);

          if (i < 8) {
            mg->Add(g, "PL");
            mh->Add(h, "E");
          } else {
            mg->Add(g, "2");
            mh->Add(h, "E");
          }
          ++colour;
          ++igeom;
        }

        gStyle->SetOptTitle(0);
        gStyle->SetPadLeftMargin(0.08);
        gStyle->SetPadRightMargin(0.05);
        gPad->SetTickx();
        gPad->SetTicky();
        gStyle->SetLegendTextSize(0.025);

        double max = 6;
        double min = -4;
        if (Variable == "DrmsNR") {
          if (variable.Contains("delta")) {
            max = 0.15;
            min = -0.1;
          } else {
            max = 1.2;
            min = 0.6;
          }
        }
        double range = max - min;

        if (((variable == "sigma" || variable == "sigmaplus" || variable == "sigmaminus" ||
              variable == "sigmadeltamu") &&
             range >= 2)) {
          mg->SetMaximum(4);
          mg->SetMinimum(-2);
        } else {
          mg->SetMaximum(max + range * 0.1);
          mg->SetMinimum(min - range * 0.3);
        }

        if (i < 8) {
          mg->Draw("a");
        } else {
          mg->Draw("a2");
        }

        char *Ytitle = (char *)YaxisNames.at(i).c_str();
        mg->GetYaxis()->SetTitle(Ytitle);
        mg->GetXaxis()->SetTitle(showlumi ? "Integrated lumi [1/fb]" : "IOV number");
        mg->GetXaxis()->CenterTitle(true);
        mg->GetYaxis()->CenterTitle(true);
        mg->GetYaxis()->SetTitleOffset(.5);
        mg->GetYaxis()->SetTitleSize(.05);
        mg->GetXaxis()->SetTitleSize(.04);
        if (showlumi)
          mg->GetXaxis()->SetLimits(0., mg->GetXaxis()->GetXmax());

        c->Update();

	TLegend *legend = c->BuildLegend(0.08,0.1,0.25,0.3);
        // TLegend *legend = c->BuildLegend(0.15,0.18,0.15,0.18);
        int Ngeom = geometries.size();
        if (Ngeom >= 6)
          legend->SetNColumns(2);
        else if (Ngeom >= 9)
          legend->SetNColumns(3);
        else
          legend->SetNColumns(1);
        TString structtitle = "#bf{";
        if (structure.Contains("PIX") && !(structure.Contains("_y")))
          structtitle += structure + " (x)";
        else if (structure.Contains("_y")) {
          TString substring(structure(0, 4));
          structtitle += substring + " (y)";
        } else
          structtitle += structure;
        if (layer != 0) {
          if (structure == "TID" || structure == "TEC" || structure == "FPIX" || structure == "FPIX_y")
            structtitle += "  disc ";
          else
            structtitle += "  layer ";
          structtitle += layer;
        }
        structtitle += "}";
        PixelUpdateLines(c, lumifile, showlumi, pixelupdateruns);

 TLine *lineOla2 = new TLine();
    lineOla2->SetLineColor(kBlack);
    lineOla2->SetLineStyle(1);
    lineOla2->SetLineWidth(4);
        legend->AddEntry(lineOla2,"First run of the year","l");

    TLine *lineOla = new TLine();
    lineOla->SetLineColor(kBlack);
    lineOla->SetLineStyle(3);
        legend->AddEntry(lineOla,"Pixel calibration update","l");

    TLine *lineOla3 = new TLine();
    lineOla3->SetLineColor(kYellow+1);
    lineOla3->SetLineStyle(1);
        legend->AddEntry(lineOla3,"Base Run for pixel template","l");

    TLine *lineOla4 = new TLine();
    lineOla4->SetLineColor(kRed+1);
    lineOla4->SetLineStyle(1);
    legend->AddEntry(lineOla4,"Problematic base runs","l");


    legend->Draw();

    double LumiTot= getintegratedlumiuptorun(-1,lumifile);
    LumiTot=LumiTot/lumiFactor;
        if (variable == "sigma" || variable == "sigmaplus" || variable == "sigmaminus" ||
              variable == "sigmadeltamu" || variable=="deltamu" || variable=="deltamusigmadeltamu") {
	  TLine *line = new TLine(0.,0.,LumiTot,0.);
	  line->SetLineColor(kMagenta);
	  line->Draw();
	}

        if (Variable =="median")
	  {     
	    if ( variable == "musigma" || variable == "muminus" || variable =="mu" || 
		 variable == "muminussigmaminus" || variable=="muplus" || variable=="muplussigmaplus") {
	      TLine *line = new TLine(0.,0.,LumiTot,0.);
	      line->SetLineColor(kMagenta);
	      line->Draw();
	    }
	  }
	
        if (Variable =="DrmsNR"){      
	  if ( variable == "musigma" ||  variable == "muminus" || variable=="mu" ||
	       variable == "muminussigmaminus" || variable=="muplus" || variable=="muplussigmaplus") {
	    TLine *line = new TLine(0.,1.,LumiTot,1.);
	    line->SetLineColor(kMagenta);
	    line->Draw();
	  }
	}

	TPaveText *CMSworkInProgress = new TPaveText(
           0, mg->GetYaxis()->GetXmax() + range * 0.02, 2.5, mg->GetYaxis()->GetXmax() + range * 0.12, "nb");
      
        CMSworkInProgress->AddText("#scale[1.1]{CMS} #it{Preliminary}");//ola changed it for public plots
        CMSworkInProgress->SetTextAlign(12);
        CMSworkInProgress->SetTextSize(0.04);
        CMSworkInProgress->SetFillColor(10);
        //CMSworkInProgress->Draw();
        TPaveText *TopRightCorner = new TPaveText(0.85 * (mg->GetXaxis()->GetXmax()),                       
                                                  mg->GetYaxis()->GetXmax() + range * 0.08,
                                                  0.95 * (mg->GetXaxis()->GetXmax()),
                                                  mg->GetYaxis()->GetXmax() + range * 0.18,
                                                "nb");
	if(Year=="Run2")TopRightCorner->AddText("#bf{CMS} #it{Work in progress} (2016+2017+2018 pp collisions)");
        else TopRightCorner->AddText("#bf{CMS} #it{Work in progress} (" + Year + " pp collisions)");
        TopRightCorner->SetTextAlign(32);
        TopRightCorner->SetTextSize(0.04);
        TopRightCorner->SetFillColor(10);
        TopRightCorner->Draw();
        TPaveText *structlabel = new TPaveText(0.85 * (mg->GetXaxis()->GetXmax()),
                                               mg->GetYaxis()->GetXmin() + range * 0.02,
                                               0.85 * (mg->GetXaxis()->GetXmax()),
                                               mg->GetYaxis()->GetXmin() + range * 0.12,
                                               "nb");
        structlabel->AddText(structtitle.Data());
        structlabel->SetTextAlign(32);
        structlabel->SetTextSize(0.04);
        structlabel->SetFillColor(10);
        structlabel->Draw();

        legend->Draw();
        mh->Draw("nostack same");
        c->Update();
        TString structandlayer = getName(structure, layer, "");
        TString printfile = outputdir;
        if (!(outputdir.EndsWith("/")))
          outputdir += "/";
        printfile += Variable;
        printfile += "_";
        printfile += variable + structandlayer;
        c->SaveAs(printfile + ".pdf");
        c->SaveAs(printfile + ".eps");
        c->SaveAs(printfile + ".png");
        c->Destructor();
      }
    }
  }
  in->Close();
}

/*! \fn trends
 *  \brief main function: if no arguments are specified a default list of arguments is used, otherwise a total of 9 arguments are required:
 * @param IOVlist:                 string containing the list of IOVs separated by a ","
 * @param variables:               string containing the variables used for running, like median or DrmsNR
 * @param labels:                  string containing labels that must be part of the input files
 * @param Year:                    string containing the year of the studied runs (needed to retrieve the lumi-per-run file), use Run2 for the full Run-2 data-taking
 * @param pathtoDMRs:              string containing the path to the directory where the DMRs are stored
 * @param geometrieandcolours:     string containing the list of geometries and colors in the following way name1:color1,name2:color2 etc.
 * @param outputdirectory:         string containing the output directory for the plots
 * @param pixelupdatelist:         string containing the list of pixelupdates separated by a ","
 * @param showpixelupdate:         boolean that if set to true will allow to plot vertical lines in the canvas corresponding to the pixel updates
 * @param showlumi:                boolean, if set to false the trends will be presented in function of the run (IOV) number, if set to true the luminosity is used on the x axis
 * @param lumifile:                string, contains the name of the lumi-per-run file to be used, by default the code will look in the Alignment/OfflineValidation/data folder as its location
 * @param FORCE:              //!< boolean, if set to true the plots will be made regardless of possible errors.
 *                                 Eventual errors while running the code will be ignored and just warnings will appear in the output.
 */


int trends(int argc, char *argv[]) {
  // parse the command line
  Options options;
  options.helper(argc, argv);
  options.parser(argc, argv);

  //Read in AllInOne json config  
  pt::ptree main_tree;
  pt::read_json(options.config, main_tree);

  pt::ptree alignments = main_tree.get_child("alignments");
  pt::ptree validation = main_tree.get_child("validation");

  //Read arguments from config
  std::vector<int> IOVlist = getVector<int>(validation, "IOV");
  std::vector<std::string> variables = getVector<std::string>(validation, "variables");
  std::vector<int> pixelUpdates = validation.get_child_optional("pixelupdatelist") ? getVector<int>(validation, "pixelupdatelist") : std::vector<int>();

  std::vector<string> geometries; std::vector<Color_t> colors;

  for(const std::string& key: getKeys(alignments)){
    geometries.push_back(alignments.get_child(key).get<std::string>("title"));
    colors.push_back(alignments.get_child(key).get<Color_t>("color"));
  } 

  std::vector<std::string> inputFiles = getVector<std::string>(main_tree, "input");
  TString outDir = main_tree.get<std::string>("output");
  TString year = validation.get<std::string>("year");
  bool showLumi = validation.get_child_optional("showlumi") ? validation.get<bool>("showlumi") : true;
  std::string lumifile = validation.get_child_optional("lumifile") ? validation.get<std::string>("lumifile") : std::string(std::getenv("CMSSW_BASE")) + "/src/Alignment/OfflineValidation/data/lumiperFullRun2.txt";
    
  DMRtrends(IOVlist,
            variables,
            year,
            inputFiles,
            geometries,
            colors,
            outDir,
            pixelUpdates,
            showLumi, 
            lumifile,
            false);

  return EXIT_SUCCESS;
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS
int main (int argc, char* argv[])
{
    return exceptions<trends>(argc, argv);
}
#endif
