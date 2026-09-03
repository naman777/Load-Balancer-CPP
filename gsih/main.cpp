// Start of HEAD
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <json/json.h>  // jsoncpp (provided on HackerRank)

using namespace std;

int main() {
    // Read all input from stdin
    string input_str((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());
    Json::Value input_data;
    Json::CharReaderBuilder rb;
    string errs;
    istringstream ss(input_str);
    Json::parseFromStream(rb, ss, &input_data, &errs);

    double mapW = input_data["map_size"][0].asDouble();
    double mapH = input_data["map_size"][1].asDouble();
    double warehouseX = mapW / 2.0, warehouseY = mapH / 2.0;
    Json::Value drones = input_data["drones"];
    Json::Value deliveries = input_data["deliveries"];
    Json::Value no_fly_zones = input_data.get("no_fly_zones", Json::Value(Json::arrayValue));
    Json::Value charging_stations = input_data.get("charging_stations", Json::Value(Json::arrayValue));
// End of HEAD

// Start of BODY
    /*
     * Schedule drone deliveries to maximize on-time deliveries
     * while minimizing energy and makespan.
     *
     * Input:
     *   warehouse: [x, y] - center of map, pickup/return location
     *   drones: array of {"id": str, "max_payload": double}
     *   deliveries: array of {"id": str, "x": double, "y": double, "weight": double, "deadline": double}
     *   no_fly_zones: array of {"shape": "circle"|"rectangle", "center"/"corners", "radius", "T_start", "T_end"}
     *   charging_stations: array of {"x": double, "y": double}
     *
     * Output:
     *   JSON: {"flight_manifest": [drone_entries]}
     *   Each drone_entry: {"drone_id": str, "path": [steps]}
     *   Each step: {"x": double, "y": double, "t": double, "action": str, ...}
     *   Actions: PICKUP (+delivery_ids), DELIVER (+delivery_id), RETURN, CHARGE, CHARGE_COMPLETE, WAIT, WAYPOINT
     *
     * Scoring:
     *   score = (on_time_deliveries * 100) - (total_energy * 0.1) - (makespan * 0.05)
     *   energy per leg = distance * (1 + current_payload_weight)
     *   Battery capacity = 500, recharges on RETURN to warehouse
     */

    struct Del { string id; double x,y,weight,deadline; bool assigned; };
    struct Drn { string id; double max_payload; };
    struct Stn { double x,y; int slots; };
    struct NFZ { string shape; double cx,cy,r,x1,y1,x2,y2,ts,te; };

    vector<Drn> DL;
    for(auto &d:drones) DL.push_back({d["id"].asString(),d["max_payload"].asDouble()});
    vector<Del> dels;
    for(auto &d:deliveries) dels.push_back({d["id"].asString(),d["x"].asDouble(),d["y"].asDouble(),d["weight"].asDouble(),d["deadline"].asDouble(),false});
    vector<Stn> stns;
    for(auto &s:charging_stations) stns.push_back({s["x"].asDouble(),s["y"].asDouble(),s["slots"].asInt()});
    vector<NFZ> nfzs;
    for(auto &n:no_fly_zones){
        NFZ z; z.shape=n["shape"].asString(); z.ts=n["T_start"].asDouble(); z.te=n["T_end"].asDouble();
        if(z.shape=="circle"){z.cx=n["center"][0].asDouble();z.cy=n["center"][1].asDouble();z.r=n["radius"].asDouble();z.x1=z.y1=z.x2=z.y2=0;}
        else{z.x1=n["corners"][0][0].asDouble();z.y1=n["corners"][0][1].asDouble();z.x2=n["corners"][1][0].asDouble();z.y2=n["corners"][1][1].asDouble();z.cx=z.cy=z.r=0;}
        nfzs.push_back(z);
    }

    auto edist=[](double ax,double ay,double bx,double by)->double{return sqrt((ax-bx)*(ax-bx)+(ay-by)*(ay-by));};

    // Analytical NFZ segment check: does segment A->B starting at t0 hit any active NFZ?
    auto segHitsNFZ=[&](double ax,double ay,double bx,double by,double t0)->bool{
        double d=edist(ax,ay,bx,by);
        if(d<1e-9) return false;
        double ux=(bx-ax)/d, uy=(by-ay)/d;
        for(auto &z:nfzs){
            double ss=max(0.0,z.ts-t0), se=min(d,z.te-t0);
            if(ss>=se) continue;
            if(z.shape=="circle"){
                double vx=ax-z.cx, vy=ay-z.cy;
                double dvu=vx*ux+vy*uy, v2=vx*vx+vy*vy, r2=z.r*z.r;
                double disc=dvu*dvu-v2+r2;
                if(disc<0) continue;
                double sq=sqrt(disc);
                double lo=max(-dvu-sq,ss), hi=min(-dvu+sq,se);
                if(lo<hi-1e-9) return true;
            } else {
                double slo=ss, shi=se;
                if(fabs(ux)<1e-12){if(ax<=z.x1||ax>=z.x2) continue;}
                else{double a1=(z.x1-ax)/ux,a2=(z.x2-ax)/ux;if(a1>a2){double t=a1;a1=a2;a2=t;}slo=max(slo,a1);shi=min(shi,a2);if(slo>=shi) continue;}
                if(fabs(uy)<1e-12){if(ay<=z.y1||ay>=z.y2) continue;}
                else{double a1=(z.y1-ay)/uy,a2=(z.y2-ay)/uy;if(a1>a2){double t=a1;a1=a2;a2=t;}slo=max(slo,a1);shi=min(shi,a2);if(slo>=shi) continue;}
                if(slo<shi-1e-9) return true;
            }
        }
        return false;
    };

    // Find NFZ-free path from A to B at time t0. Returns waypoints (including B).
    auto findPath=[&](double ax,double ay,double bx,double by,double t0,vector<pair<double,double>>&wps)->bool{
        wps.clear();
        if(!segHitsNFZ(ax,ay,bx,by,t0)){wps.push_back({bx,by});return true;}
        vector<pair<double,double>> cands;
        for(auto &z:nfzs){
            if(z.shape=="circle"){
                double rr=z.r+2.0;
                for(int a=0;a<8;a++){double ang=a*M_PI/4.0;cands.push_back({z.cx+rr*cos(ang),z.cy+rr*sin(ang)});}
            }else{
                double m=3.0;
                cands.push_back({z.x1-m,z.y1-m});cands.push_back({z.x2+m,z.y1-m});
                cands.push_back({z.x1-m,z.y2+m});cands.push_back({z.x2+m,z.y2+m});
                cands.push_back({z.x1-m,(z.y1+z.y2)/2});cands.push_back({z.x2+m,(z.y1+z.y2)/2});
                cands.push_back({(z.x1+z.x2)/2,z.y1-m});cands.push_back({(z.x1+z.x2)/2,z.y2+m});
            }
        }
        double bd=1e18; int bi=-1;
        for(int i=0;i<(int)cands.size();i++){
            double d1=edist(ax,ay,cands[i].first,cands[i].second),d2=edist(cands[i].first,cands[i].second,bx,by);
            if(d1+d2<bd&&!segHitsNFZ(ax,ay,cands[i].first,cands[i].second,t0)&&!segHitsNFZ(cands[i].first,cands[i].second,bx,by,t0+d1)){
                bd=d1+d2;bi=i;
            }
        }
        if(bi>=0){wps.push_back(cands[bi]);wps.push_back({bx,by});return true;}
        for(int i=0;i<(int)cands.size();i++){
            if(segHitsNFZ(ax,ay,cands[i].first,cands[i].second,t0)) continue;
            double d1=edist(ax,ay,cands[i].first,cands[i].second),t1=t0+d1;
            for(int j=0;j<(int)cands.size();j++){
                double d2=edist(cands[i].first,cands[i].second,cands[j].first,cands[j].second);
                if(!segHitsNFZ(cands[i].first,cands[i].second,cands[j].first,cands[j].second,t1)&&
                   !segHitsNFZ(cands[j].first,cands[j].second,bx,by,t1+d2)){
                    wps.push_back(cands[i]);wps.push_back(cands[j]);wps.push_back({bx,by});return true;
                }
            }
        }
        return false;
    };

    // Resolve path: try direct, then detour, then wait for NFZ expiry
    auto resolvePath=[&](double ax,double ay,double bx,double by,double t0,vector<pair<double,double>>&wps,double &waitTime)->bool{
        waitTime=0;
        if(findPath(ax,ay,bx,by,t0,wps)) return true;
        // Try waiting
        double maxTE=0;
        for(auto &z:nfzs) if(z.te>t0) maxTE=max(maxTE,z.te);
        for(double w=1;w<=maxTE-t0+1;w+=1){
            if(findPath(ax,ay,bx,by,t0+w,wps)){waitTime=w;return true;}
        }
        return false;
    };

    // Compute actual path distance (with waypoints) for a leg
    auto legPathDist=[&](double ax,double ay,vector<pair<double,double>>&wps)->double{
        double d=0,cx=ax,cy=ay;
        for(auto &w:wps){d+=edist(cx,cy,w.first,w.second);cx=w.first;cy=w.second;}
        return d;
    };

    // Sort deliveries by deadline
    vector<int> delOrd(dels.size());
    for(int i=0;i<(int)delOrd.size();i++) delOrd[i]=i;
    for(int i=0;i<(int)delOrd.size();i++)
        for(int j=i+1;j<(int)delOrd.size();j++)
            if(dels[delOrd[j]].deadline<dels[delOrd[i]].deadline){int t=delOrd[i];delOrd[i]=delOrd[j];delOrd[j]=t;}

    Json::Value flight_manifest(Json::arrayValue);

    for(auto &drone:DL){
        bool hasWork=true;
        Json::Value droneEntry; droneEntry["drone_id"]=drone.id;
        Json::Value fullPath(Json::arrayValue);
        double curT=0, battery=500.0;

        while(hasWork){
            // Collect candidate deliveries for this trip
            vector<int> cands;
            double tw=0;
            for(int idx:delOrd){
                if(dels[idx].assigned) continue;
                if(tw+dels[idx].weight<=drone.max_payload+1e-9){
                    double d2=edist(warehouseX,warehouseY,dels[idx].x,dels[idx].y);
                    if(curT+d2<=dels[idx].deadline+1e-9){cands.push_back(idx);tw+=dels[idx].weight;}
                }
            }
            if(cands.empty()){hasWork=false;break;}

            // Nearest-neighbor ordering with deadline awareness
            vector<int> ordered;
            vector<bool> used(cands.size(),false);
            double sx=warehouseX,sy=warehouseY,st=curT;
            for(int r=0;r<(int)cands.size();r++){
                int bj=-1;double bs=1e18;
                for(int j=0;j<(int)cands.size();j++){
                    if(used[j]) continue;
                    int di=cands[j];
                    double d=edist(sx,sy,dels[di].x,dels[di].y),arr=st+d,sl=dels[di].deadline-arr;
                    if(sl<-1e-9) continue;
                    double sc=d-sl*0.01;
                    if(sc<bs){bs=sc;bj=j;}
                }
                if(bj<0) break;
                used[bj]=true;
                int di=cands[bj];
                double d=edist(sx,sy,dels[di].x,dels[di].y);
                sx=dels[di].x;sy=dels[di].y;st+=d;
                ordered.push_back(di);
            }
            if(ordered.empty()){hasWork=false;break;}

            // Trial run: build actual path with NFZ avoidance, compute real energy
            bool tripOK=false;
            while(!ordered.empty()){
                struct PathLeg{vector<pair<double,double>> wps; double waitTime;};
                vector<PathLeg> legs;
                double cx=warehouseX,cy=warehouseY,ct=curT;
                double payload=0; for(int di:ordered) payload+=dels[di].weight;
                double totalE=0; bool pathOK=true;

                for(int di:ordered){
                    PathLeg leg; double wt=0;
                    bool ok=resolvePath(cx,cy,dels[di].x,dels[di].y,ct,leg.wps,wt);
                    if(!ok){pathOK=false;break;}
                    leg.waitTime=wt; ct+=wt;
                    double ld=legPathDist(cx,cy,leg.wps);
                    totalE+=ld*(1.0+payload);
                    ct+=ld;
                    if(ct>dels[di].deadline+1e-9){pathOK=false;break;}
                    cx=dels[di].x;cy=dels[di].y;
                    payload-=dels[di].weight;
                    legs.push_back(leg);
                }
                if(!pathOK){ordered.pop_back();continue;}

                // Return leg
                PathLeg retLeg; double retWait=0;
                bool retOK=resolvePath(cx,cy,warehouseX,warehouseY,ct,retLeg.wps,retWait);
                if(!retOK){ordered.pop_back();continue;}
                retLeg.waitTime=retWait;
                double retDist=legPathDist(cx,cy,retLeg.wps);
                double retE=retDist*1.0; // payload=0

                // Check battery
                if(totalE+retE<=battery){
                    // Feasible without charging
                    // Build output
                    Json::Value pickup;
                    pickup["x"]=warehouseX;pickup["y"]=warehouseY;pickup["t"]=round(curT*100)/100.0;
                    pickup["action"]="PICKUP";
                    Json::Value ids(Json::arrayValue);
                    for(int di:ordered) ids.append(dels[di].id);
                    pickup["delivery_ids"]=ids;
                    fullPath.append(pickup);

                    cx=warehouseX;cy=warehouseY;ct=curT;
                    payload=0;for(int di:ordered) payload+=dels[di].weight;

                    for(int li=0;li<(int)ordered.size();li++){
                        int di=ordered[li];
                        if(legs[li].waitTime>0){
                            ct+=legs[li].waitTime;
                            Json::Value wn;wn["x"]=cx;wn["y"]=cy;wn["t"]=round(ct*100)/100.0;wn["action"]="WAIT";
                            fullPath.append(wn);
                        }
                        for(auto &wp:legs[li].wps){
                            double d=edist(cx,cy,wp.first,wp.second);ct+=d;
                            Json::Value nd;nd["x"]=wp.first;nd["y"]=wp.second;nd["t"]=round(ct*100)/100.0;
                            bool isTgt=(fabs(wp.first-dels[di].x)<1e-9&&fabs(wp.second-dels[di].y)<1e-9);
                            if(isTgt){nd["action"]="DELIVER";nd["delivery_id"]=dels[di].id;}
                            else{nd["action"]="WAYPOINT";}
                            fullPath.append(nd);cx=wp.first;cy=wp.second;
                        }
                        payload-=dels[di].weight;
                    }
                    // Return
                    if(retLeg.waitTime>0){
                        ct+=retLeg.waitTime;
                        Json::Value wn;wn["x"]=cx;wn["y"]=cy;wn["t"]=round(ct*100)/100.0;wn["action"]="WAIT";
                        fullPath.append(wn);
                    }
                    for(int i=0;i<(int)retLeg.wps.size();i++){
                        double d=edist(cx,cy,retLeg.wps[i].first,retLeg.wps[i].second);ct+=d;
                        Json::Value nd;nd["x"]=retLeg.wps[i].first;nd["y"]=retLeg.wps[i].second;nd["t"]=round(ct*100)/100.0;
                        nd["action"]=(i==(int)retLeg.wps.size()-1?"RETURN":"WAYPOINT");
                        fullPath.append(nd);cx=retLeg.wps[i].first;cy=retLeg.wps[i].second;
                    }
                    for(int di:ordered) dels[di].assigned=true;
                    curT=ct;battery=500.0;
                    tripOK=true;break;
                } else {
                    // Try charging station
                    int bestS=-1;double bestSD=1e18;
                    for(int i=0;i<(int)stns.size();i++){
                        double d=edist(cx,cy,stns[i].x,stns[i].y);
                        if(d<bestSD){bestSD=d;bestS=i;}
                    }
                    if(bestS>=0){
                        double eToStn=bestSD*1.0;
                        if(totalE+eToStn<=battery){
                            // Can reach station, charge, then return
                            PathLeg stnToWH; double stwWait=0;
                            double stnX=stns[bestS].x,stnY=stns[bestS].y;
                            double ctAtStn=ct+retWait+bestSD;
                            bool stwOK=resolvePath(stnX,stnY,warehouseX,warehouseY,ctAtStn+50,stnToWH.wps,stwWait);
                            double stwDist=stwOK?legPathDist(stnX,stnY,stnToWH.wps):edist(stnX,stnY,warehouseX,warehouseY);
                            double stwE=stwDist*1.0;
                            double batAtStn=battery-totalE-eToStn;
                            double needed=max(0.0,stwE-batAtStn);
                            double chargeT=ceil(needed/2.0);

                            // Build output with charging
                            Json::Value pickup;
                            pickup["x"]=warehouseX;pickup["y"]=warehouseY;pickup["t"]=round(curT*100)/100.0;
                            pickup["action"]="PICKUP";
                            Json::Value ids(Json::arrayValue);
                            for(int di:ordered) ids.append(dels[di].id);
                            pickup["delivery_ids"]=ids;
                            fullPath.append(pickup);

                            cx=warehouseX;cy=warehouseY;ct=curT;
                            payload=0;for(int di:ordered) payload+=dels[di].weight;

                            for(int li=0;li<(int)ordered.size();li++){
                                int di=ordered[li];
                                if(legs[li].waitTime>0){
                                    ct+=legs[li].waitTime;
                                    Json::Value wn;wn["x"]=cx;wn["y"]=cy;wn["t"]=round(ct*100)/100.0;wn["action"]="WAIT";
                                    fullPath.append(wn);
                                }
                                for(auto &wp:legs[li].wps){
                                    double d=edist(cx,cy,wp.first,wp.second);ct+=d;
                                    Json::Value nd;nd["x"]=wp.first;nd["y"]=wp.second;nd["t"]=round(ct*100)/100.0;
                                    bool isTgt=(fabs(wp.first-dels[di].x)<1e-9&&fabs(wp.second-dels[di].y)<1e-9);
                                    if(isTgt){nd["action"]="DELIVER";nd["delivery_id"]=dels[di].id;}
                                    else{nd["action"]="WAYPOINT";}
                                    fullPath.append(nd);cx=wp.first;cy=wp.second;
                                }
                                payload-=dels[di].weight;
                            }
                            // Fly to station
                            if(bestSD>1e-9){
                                ct+=bestSD;
                                Json::Value nd;nd["x"]=stnX;nd["y"]=stnY;nd["t"]=round(ct*100)/100.0;nd["action"]="WAYPOINT";
                                fullPath.append(nd);cx=stnX;cy=stnY;
                            }
                            {Json::Value cn;cn["x"]=cx;cn["y"]=cy;cn["t"]=round(ct*100)/100.0;cn["action"]="CHARGE";fullPath.append(cn);}
                            ct+=chargeT;
                            {Json::Value cn;cn["x"]=cx;cn["y"]=cy;cn["t"]=round(ct*100)/100.0;cn["action"]="CHARGE_COMPLETE";fullPath.append(cn);}

                            // Return from station
                            if(stwOK&&stwWait>0){ct+=stwWait;Json::Value wn;wn["x"]=cx;wn["y"]=cy;wn["t"]=round(ct*100)/100.0;wn["action"]="WAIT";fullPath.append(wn);}
                            auto &rwps=stwOK?stnToWH.wps:retLeg.wps;
                            double rcx=cx,rcy=cy;
                            if(!stwOK){rwps.clear();rwps.push_back({warehouseX,warehouseY});}
                            for(int i=0;i<(int)rwps.size();i++){
                                double d=edist(rcx,rcy,rwps[i].first,rwps[i].second);ct+=d;
                                Json::Value nd;nd["x"]=rwps[i].first;nd["y"]=rwps[i].second;nd["t"]=round(ct*100)/100.0;
                                nd["action"]=(i==(int)rwps.size()-1?"RETURN":"WAYPOINT");
                                fullPath.append(nd);rcx=rwps[i].first;rcy=rwps[i].second;
                            }
                            for(int di:ordered) dels[di].assigned=true;
                            curT=ct;battery=500.0;
                            tripOK=true;break;
                        }
                    }
                    if(!tripOK) ordered.pop_back();
                }
            }
            if(!tripOK){hasWork=false;break;}
        }
        if(fullPath.size()>0){droneEntry["path"]=fullPath;flight_manifest.append(droneEntry);}
    }

// End of BODY

// Start of TAIL
    Json::Value output;
    output["flight_manifest"] = flight_manifest;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    cout << Json::writeString(wb, output) << endl;
    return 0;
}
// End of TAIL