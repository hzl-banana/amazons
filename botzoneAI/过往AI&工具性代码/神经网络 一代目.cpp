#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>
#include <fstream>

using namespace std;

/**
 * Amazons inference player with MCTS using trained NN value.
 * Requires two binary weight files:
 *   hidden.bin  (float32, shape = [29,65,256], C-order)
 *   output.bin  (float32, shape = [3,256], C-order)
 *
 * Export from Python (after training):
 * ------------------------------------------------------------------
 * import pickle, numpy as np
 * d = pickle.load(open("./trained_models/model_XXXX.pkl","rb"))
 * d["hidden_weights"].astype(np.float32).tofile("hidden.bin")
 * d["output_weights"].astype(np.float32).tofile("output.bin")
 * ------------------------------------------------------------------
 *
 * Compile: g++ -O3 -std=c++17 amazons_infer_mcts.cpp -o amazons_infer_mcts
 */

constexpr int GRIDSIZE = 8;
constexpr int OBSTACLE = 2;
constexpr int grid_black = 1;
constexpr int grid_white = -1;

int gridInfo[GRIDSIZE][GRIDSIZE]{};
int dx8[] = {-1,-1,-1,0,0,1,1,1};
int dy8[] = {-1,0,1,-1,1,-1,0,1};

inline bool inMap(int x,int y){return x>=0&&x<GRIDSIZE&&y>=0&&y<GRIDSIZE;}
bool ProcStep(int x0,int y0,int x1,int y1,int x2,int y2,int color,bool check_only){
    if(!inMap(x0,y0)||!inMap(x1,y1)||!inMap(x2,y2))return false;
    if(gridInfo[x0][y0]!=color||gridInfo[x1][y1]!=0)return false;
    if(gridInfo[x2][y2]!=0 && !(x2==x0 && y2==y0))return false;
    if(!check_only){
        gridInfo[x0][y0]=0;
        gridInfo[x1][y1]=color;
        gridInfo[x2][y2]=OBSTACLE;
    }
    return true;
}

// NN dims
constexpr int UNITS=29, TUPLES=65, HIDDEN=256, BUCKETS=3;
vector<float> hidden_w, output_w;

// bitboard + features
using U64 = unsigned long long;
constexpr U64 LEFT_MASK  = 0x7f7f7f7f7f7f7f7fULL;
constexpr U64 RIGHT_MASK = 0xfefefefefefefefeULL;
inline int idx_from_xy(int x,int y){return 8*(7 - x) + (7 - y);}

struct BitBoard {
    U64 one=0,two=0,arrow=0;
    void from_grid(){
        one=two=arrow=0;
        for(int x=0;x<GRIDSIZE;++x)for(int y=0;y<GRIDSIZE;++y){
            int v=gridInfo[x][y]; U64 bit=1ULL<<idx_from_xy(x,y);
            if(v==grid_white) one|=bit;
            else if(v==grid_black) two|=bit;
            else if(v==OBSTACLE) arrow|=bit;
        }
    }
    U64 shift(U64 d,int dir)const{
        static const int dirs[8]={1,-1,8,-8,9,7,-9,-7};
        static const U64 masks[8]={RIGHT_MASK,LEFT_MASK,
                                   0xFFFFFFFFFFFFFFFFULL,0xFFFFFFFFFFFFFFFFULL,
                                   RIGHT_MASK,LEFT_MASK,LEFT_MASK,RIGHT_MASK};
        int dd=dirs[dir]; U64 m=masks[dir];
        return dd>0? ((d<<dd)&m):((d>>(-dd))&m);
    }
    U64 gen_reach(U64 s)const{
        U64 empty=~(one|two|arrow), legal=0;
        for(int dir=0;dir<8;++dir){
            U64 x=shift(s,dir)&empty;
            for(int k=0;k<6;++k) x |= shift(x,dir)&empty;
            legal |= x;
        }
        return legal;
    }
    void to_tuples(int cur_player,int rounds,array<int,TUPLES>& t)const{
        U64 mine = (cur_player==0)? one: two;
        U64 opp  = (cur_player==0)? two: one;
        U64 a=gen_reach(mine), a2=gen_reach(a|mine);
        U64 b=gen_reach(opp),  b2=gen_reach(b|opp);
        for(int i=0;i<64;++i){
            U64 p=1ULL<<i;
            if(mine&p){
                U64 r=gen_reach(p); int mob=min(9,__builtin_popcountll(r));
                t[i]=1+mob;
            }else if(opp&p){
                U64 r=gen_reach(p); int mob=min(9,__builtin_popcountll(r));
                t[i]=10+mob;
            }else if(arrow&p){
                t[i]=20;
            }else{
                bool A=a&p,B=b&p,A2=a2&p,B2=b2&p;
                if(A&&B) t[i]=21;
                else if(A) t[i]=B2?22:23;
                else if(B) t[i]=A2?24:25;
                else if(A2&&B2) t[i]=26;
                else if(A2) t[i]=27;
                else if(B2) t[i]=28;
                else t[i]=0;
            }
        }
        if(rounds<14) t[64]=0; else if(rounds<28) t[64]=1; else t[64]=2;
    }
};

inline float relu2(float x){ if(x<0) return 0.f; if(x>=1) return x; return x*x; }
inline float fast_tanh(float x){ return std::tanh(x); }

float eval_state(const array<int,TUPLES>& ts){
    float h[HIDDEN]; fill(h,h+HIDDEN,0.f);
    for(int j=0;j<TUPLES;++j){
        int idx=ts[j]; size_t base=(static_cast<size_t>(idx)*TUPLES + j)*HIDDEN;
        for(int k=0;k<HIDDEN;++k) h[k]+=hidden_w[base+k];
    }
    for(int k=0;k<HIDDEN;++k) h[k]=relu2(h[k]);
    int b=ts[64]; size_t ob=(static_cast<size_t>(b))*HIDDEN;
    float out=0; for(int k=0;k<HIDDEN;++k) out+=output_w[ob+k]*h[k];
    return fast_tanh(out);
}

bool load_bin(const string& p, vector<float>& buf, size_t expect){
    ifstream f(p,ios::binary); if(!f) return false;
    f.seekg(0,ios::end); size_t sz=static_cast<size_t>(f.tellg()); f.seekg(0,ios::beg);
    if(sz!=expect*sizeof(float)) return false;
    buf.resize(expect); f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz)); return true;
}

// Move representation
struct Move{int x0,y0,x1,y1,x2,y2;};

// MCTS node
struct Node{
    Move mv{};
    float value_sum=0;
    int visits=0;
    float prior=0;
    bool terminal=false;
    vector<unique_ptr<Node>> ch;
};

float UCT(const Node& n, int parent_visits, float c_puct=1.0f){
    if(n.visits==0) return n.prior * c_puct * std::sqrt(static_cast<float>(parent_visits) + 1e-6f);
    return (n.value_sum / n.visits) + c_puct * n.prior * std::sqrt(static_cast<float>(parent_visits))/(1+n.visits);
}

// Generate all moves for color
vector<Move> gen_moves(int color){
    vector<Move> ms;
    for(int i=0;i<GRIDSIZE;++i)for(int j=0;j<GRIDSIZE;++j){
        if(gridInfo[i][j]!=color) continue;
        for(int k=0;k<8;++k){
            for(int d1=1; d1<GRIDSIZE; ++d1){
                int xx=i+dx8[k]*d1, yy=j+dy8[k]*d1;
                if(!inMap(xx,yy)||gridInfo[xx][yy]!=0) break;
                for(int l=0;l<8;++l){
                    for(int d2=1; d2<GRIDSIZE; ++d2){
                        int xxx=xx+dx8[l]*d2, yyy=yy+dy8[l]*d2;
                        if(!inMap(xxx,yyy)) break;
                        if(gridInfo[xxx][yyy]!=0 && !(xxx==i && yyy==j)) break;
                        if(ProcStep(i,j,xx,yy,xxx,yyy,color,true))
                            ms.push_back({i,j,xx,yy,xxx,yyy});
                    }
                }
            }
        }
    }
    return ms;
}

void apply_move(const Move& m,int color){ ProcStep(m.x0,m.y0,m.x1,m.y1,m.x2,m.y2,color,false); }
void undo_move(const Move& m,int color){ gridInfo[m.x0][m.y0]=color; gridInfo[m.x1][m.y1]=0; gridInfo[m.x2][m.y2]=0; }

// Expand node
void expand(Node& node, int to_move, int rounds){
    auto moves = gen_moves(to_move);
    if(moves.empty()){
        node.terminal=true; return;
    }
    float prior = 1.0f / static_cast<float>(moves.size());
    node.ch.reserve(moves.size());
    for(auto& m: moves){
        auto child = std::make_unique<Node>();
        child->mv = m;
        child->prior = prior;
        node.ch.push_back(std::move(child));
    }
}

// Evaluate leaf with NN (current player to move = cur_player)
float evaluate_leaf(int cur_player, int rounds){
    BitBoard bb; bb.from_grid();
    array<int,TUPLES> ts;
    bb.to_tuples(cur_player==grid_white?0:1, rounds, ts);
    return eval_state(ts);
}

// One simulation
float simulate(Node& root, int to_move, int rounds, float c_puct){
    vector<Node*> path;
    Node* node=&root; int cur = to_move; int rd=rounds;
    // selection
    while(!node->ch.empty() && !node->terminal){
        int parent_visits = std::max(1, node->visits);
        Node* best=nullptr; float best_u=-1e9f;
        for(auto& ch: node->ch){
            float u = UCT(*ch, parent_visits, c_puct);
            if(u>best_u){ best_u=u; best=ch.get(); }
        }
        if(!best) break;
        apply_move(best->mv, cur);
        path.push_back(best);
        node = best;
        cur = -cur;
        ++rd;
    }
    // expand/eval
    float value;
    if(node->terminal){
        value = -1.0f; // current player loses
    }else{
        expand(*node, cur, rd);
        if(node->terminal){
            value = -1.0f;
        }else if(!node->ch.empty()){
            apply_move(node->ch[0]->mv, cur);
            value = evaluate_leaf(-cur, rd+1); // from next player perspective
            undo_move(node->ch[0]->mv, cur);
        }else{
            value = 0.0f;
        }
    }
    // backprop (value from perspective of root player to_move)
    int sign = 1;
    for(auto it=path.rbegin(); it!=path.rend(); ++it){
        (*it)->visits += 1;
        (*it)->value_sum += value * sign;
        sign = -sign;
        undo_move((*it)->mv, sign==1?to_move:-to_move);
    }
    root.visits +=1;
    root.value_sum += value;
    return value;
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    const size_t HW_SZ = static_cast<size_t>(UNITS) * TUPLES * HIDDEN;
    const size_t OW_SZ = static_cast<size_t>(BUCKETS) * HIDDEN;
    if(!load_bin("hidden.bin", hidden_w, HW_SZ) ||
       !load_bin("output.bin", output_w, OW_SZ)){
        cerr<<"Failed to load hidden.bin/output.bin\n"; return 1;
    }

    // init board same as原框架
    gridInfo[0][(GRIDSIZE - 1) / 3] = grid_black;
    gridInfo[(GRIDSIZE - 1) / 3][0] = grid_black;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][0] = grid_black;
    gridInfo[GRIDSIZE - 1][(GRIDSIZE - 1) / 3] = grid_black;

    gridInfo[0][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;
    gridInfo[(GRIDSIZE - 1) / 3][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)][GRIDSIZE - 1] = grid_white;
    gridInfo[GRIDSIZE - 1][GRIDSIZE - 1 - ((GRIDSIZE - 1) / 3)] = grid_white;

    int turnID; if(!(cin>>turnID)) return 0;
    int currBotColor = grid_white;
    int rounds = 0;
    int x0,y0,x1,y1,x2,y2;
    for(int i=0;i<turnID;++i){
        cin>>x0>>y0>>x1>>y1>>x2>>y2;
        if(x0==-1) { currBotColor=grid_black; }
        else { ProcStep(x0,y0,x1,y1,x2,y2,-currBotColor,false); ++rounds; }
        if(i<turnID-1){
            cin>>x0>>y0>>x1>>y1>>x2>>y2;
            if(x0>=0){ ProcStep(x0,y0,x1,y1,x2,y2,currBotColor,false); ++rounds; }
        }
    }

    // root node expand
    Node root;
    expand(root, currBotColor, rounds);
    if(root.terminal || root.ch.empty()){
        cout << "-1 -1 -1 -1 -1 -1\n"; return 0;
    }

    // MCTS params (adjust for speed/strength)
    int simulations = 400;
    float c_puct = 1.0f;
    for(int s=0;s<simulations;++s){
        simulate(root, currBotColor, rounds, c_puct);
    }

    // pick best by visits
    int bestIdx=-1; int bestVisit=-1;
    for(size_t i=0;i<root.ch.size();++i){
        if(root.ch[i]->visits > bestVisit){
            bestVisit = root.ch[i]->visits;
            bestIdx = static_cast<int>(i);
        }
    }
    Move best = root.ch[bestIdx]->mv;
    cout << best.x0 << ' ' << best.y0 << ' ' << best.x1 << ' ' << best.y1
         << ' ' << best.x2 << ' ' << best.y2 << "\n";
    return 0;
}