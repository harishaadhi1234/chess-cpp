# chess-cpp
Two-player chess in C++ with full move validation
/*
 * chess.cpp — Two-Player Console Chess in C++
 *
 * Features:
 *   - Full piece movement for all 6 piece types
 *   - Legal move validation (no moving into check)
 *   - Castling (kingside & queenside)
 *   - En passant
 *   - Pawn promotion (auto-queen)
 *   - Check, checkmate, stalemate detection
 *   - Captured pieces display
 *
 * Compile:  g++ -std=c++17 -o chess chess.cpp
 * Run:      ./chess
 * Input:    e2e4  (from-square to-square, e.g. "e2e4", "e1g1" for castling)
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>

// ─── Constants ────────────────────────────────────────────────────────────────

enum Color { NONE = 0, WHITE, BLACK };
enum Type  { EMPTY = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

struct Piece {
    Color color = NONE;
    Type  type  = EMPTY;
    bool operator==(const Piece& o) const { return color==o.color && type==o.type; }
};

const Piece EMPTY_PIECE = {NONE, EMPTY};

struct Square { int r, f; };          // r = rank 0-7 (0=rank8), f = file 0-7 (0=a)
struct Move   { Square from, to; };

// ─── Board ────────────────────────────────────────────────────────────────────

using Board = Piece[8][8];

inline bool inBounds(int r, int f) { return r>=0 && r<8 && f>=0 && f<8; }
inline Color opp(Color c) { return c==WHITE ? BLACK : WHITE; }

void copyBoard(const Board src, Board dst) {
    for(int r=0;r<8;r++) for(int f=0;f<8;f++) dst[r][f]=src[r][f];
}

// Unicode chess symbols
const char* pieceSymbol(const Piece& p) {
    if(p.color==WHITE) switch(p.type){
        case PAWN:   return "♙"; case KNIGHT: return "♘";
        case BISHOP: return "♗"; case ROOK:   return "♖";
        case QUEEN:  return "♕"; case KING:   return "♔";
        default: break;
    }
    if(p.color==BLACK) switch(p.type){
        case PAWN:   return "♟"; case KNIGHT: return "♞";
        case BISHOP: return "♝"; case ROOK:   return "♜";
        case QUEEN:  return "♛"; case KING:   return "♚";
        default: break;
    }
    return ".";
}

// ─── Game State ───────────────────────────────────────────────────────────────

struct GameState {
    Board board;
    Color turn = WHITE;
    // Castling rights
    bool castleWK = true, castleWQ = true;
    bool castleBK = true, castleBQ = true;
    // En passant target square (-1 if none)
    int epRank = -1, epFile = -1;
    // Captured pieces
    std::vector<Piece> capturedByWhite, capturedByBlack;
};

void initBoard(GameState& gs) {
    // Clear
    for(int r=0;r<8;r++) for(int f=0;f<8;f++) gs.board[r][f]=EMPTY_PIECE;

    Type backRank[] = {ROOK,KNIGHT,BISHOP,QUEEN,KING,BISHOP,KNIGHT,ROOK};
    for(int f=0;f<8;f++){
        gs.board[0][f] = {BLACK, backRank[f]};
        gs.board[1][f] = {BLACK, PAWN};
        gs.board[6][f] = {WHITE, PAWN};
        gs.board[7][f] = {WHITE, backRank[f]};
    }
    gs.turn = WHITE;
    gs.castleWK = gs.castleWQ = gs.castleBK = gs.castleBQ = true;
    gs.epRank = gs.epFile = -1;
    gs.capturedByWhite.clear();
    gs.capturedByBlack.clear();
}

// ─── Raw Move Generation (ignores check) ──────────────────────────────────────

void addIfInBounds(std::vector<Move>& moves, Square from, int nr, int nf,
                   const Board& b, Color myColor, bool capturesOnly=false)
{
    if(!inBounds(nr,nf)) return;
    Color there = b[nr][nf].color;
    if(there == myColor) return;            // can't capture own piece
    if(capturesOnly && there == NONE) return;
    moves.push_back({from, {nr,nf}});
}

void slide(std::vector<Move>& moves, Square from, int dr, int df,
           const Board& b, Color myColor)
{
    int nr=from.r+dr, nf=from.f+df;
    while(inBounds(nr,nf)){
        Color there = b[nr][nf].color;
        if(there == myColor) break;
        moves.push_back({from, {nr,nf}});
        if(there != NONE) break;            // blocked after capture
        nr+=dr; nf+=df;
    }
}

// Generate pseudo-legal moves for piece at (r,f); noKingCheck skips castling
std::vector<Move> getRawMoves(const GameState& gs, int r, int f, bool noKingCheck=false) {
    std::vector<Move> moves;
    const Piece& p = gs.board[r][f];
    if(p.type == EMPTY) return moves;
    Color c = p.color;
    Square from = {r,f};

    switch(p.type){

    case PAWN: {
        int dir = (c==WHITE) ? -1 : 1;
        int startRank = (c==WHITE) ? 6 : 1;
        // Forward one
        if(inBounds(r+dir,f) && gs.board[r+dir][f].type==EMPTY){
            moves.push_back({from,{r+dir,f}});
            // Forward two from start
            if(r==startRank && gs.board[r+2*dir][f].type==EMPTY)
                moves.push_back({from,{r+2*dir,f}});
        }
        // Diagonal captures
        for(int df : {-1,1}){
            if(!inBounds(r+dir,f+df)) continue;
            Color there = gs.board[r+dir][f+df].color;
            if(there==opp(c))
                moves.push_back({from,{r+dir,f+df}});
            // En passant
            if(!noKingCheck && gs.epRank==r+dir && gs.epFile==f+df)
                moves.push_back({from,{r+dir,f+df}});
        }
        break;
    }

    case KNIGHT: {
        int deltas[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
        for(auto& d : deltas) addIfInBounds(moves,from,r+d[0],f+d[1],gs.board,c);
        break;
    }

    case BISHOP:
        for(auto [dr,df] : std::vector<std::pair<int,int>>{{-1,-1},{-1,1},{1,-1},{1,1}})
            slide(moves,from,dr,df,gs.board,c);
        break;

    case ROOK:
        for(auto [dr,df] : std::vector<std::pair<int,int>>{{-1,0},{1,0},{0,-1},{0,1}})
            slide(moves,from,dr,df,gs.board,c);
        break;

    case QUEEN:
        for(auto [dr,df] : std::vector<std::pair<int,int>>{
            {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}})
            slide(moves,from,dr,df,gs.board,c);
        break;

    case KING: {
        for(auto [dr,df] : std::vector<std::pair<int,int>>{
            {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}})
            addIfInBounds(moves,from,r+dr,f+df,gs.board,c);
        // Castling handled in getLegalMoves (needs isAttacked check)
        break;
    }

    default: break;
    }
    return moves;
}

// ─── Attack / Check Detection ─────────────────────────────────────────────────

bool isAttacked(const GameState& gs, int r, int f, Color byColor) {
    // Temporarily make a state with no ep so pawn attacks don't wrongly trigger
    GameState tmp; copyBoard(gs.board, tmp.board);
    tmp.epRank = tmp.epFile = -1;
    tmp.castleWK=tmp.castleWQ=tmp.castleBK=tmp.castleBQ=false;
    tmp.turn = byColor;

    for(int rr=0;rr<8;rr++) for(int ff=0;ff<8;ff++){
        if(tmp.board[rr][ff].color != byColor) continue;
        auto mvs = getRawMoves(tmp, rr, ff, true);
        for(auto& m : mvs)
            if(m.to.r==r && m.to.f==f) return true;
    }
    return false;
}

Square findKing(const Board& b, Color c) {
    for(int r=0;r<8;r++) for(int f=0;f<8;f++)
        if(b[r][f].color==c && b[r][f].type==KING) return {r,f};
    return {-1,-1};
}

bool inCheck(const GameState& gs, Color c) {
    Square k = findKing(gs.board, c);
    if(k.r<0) return false;
    return isAttacked(gs, k.r, k.f, opp(c));
}

// ─── Legal Move Generation ────────────────────────────────────────────────────

// Apply a pseudo-legal move to a copy and return new state (no update of captured/ep)
GameState applyMoveTemp(const GameState& gs, const Move& m) {
    GameState next = gs;
    Piece p = next.board[m.from.r][m.from.f];
    int r=m.from.r, f=m.from.f, nr=m.to.r, nf=m.to.f;

    // En passant capture
    if(p.type==PAWN && nf!=f && next.board[nr][nf].type==EMPTY)
        next.board[r][nf] = EMPTY_PIECE;

    next.board[nr][nf] = p;
    next.board[r][f]   = EMPTY_PIECE;

    // Pawn promotion
    if(p.type==PAWN && (nr==0||nr==7))
        next.board[nr][nf] = {p.color, QUEEN};

    return next;
}

std::vector<Move> getLegalMoves(const GameState& gs, int r, int f) {
    std::vector<Move> legal;
    if(gs.board[r][f].color != gs.turn) return legal;

    auto raw = getRawMoves(gs, r, f);
    for(auto& m : raw){
        GameState tmp = applyMoveTemp(gs, m);
        if(!inCheck(tmp, gs.turn))
            legal.push_back(m);
    }

    // Castling (must be done separately due to isAttacked dependency)
    Piece p = gs.board[r][f];
    if(p.type==KING && !inCheck(gs, p.color)){
        Color c = p.color;
        int row = (c==WHITE) ? 7 : 0;
        if(r==row && f==4){
            // Kingside
            bool canKS = (c==WHITE ? gs.castleWK : gs.castleBK);
            if(canKS && gs.board[row][5].type==EMPTY && gs.board[row][6].type==EMPTY
               && !isAttacked(gs,row,5,opp(c)) && !isAttacked(gs,row,6,opp(c)))
                legal.push_back({{r,f},{row,6}});
            // Queenside
            bool canQS = (c==WHITE ? gs.castleWQ : gs.castleBQ);
            if(canQS && gs.board[row][3].type==EMPTY && gs.board[row][2].type==EMPTY
               && gs.board[row][1].type==EMPTY
               && !isAttacked(gs,row,3,opp(c)) && !isAttacked(gs,row,2,opp(c)))
                legal.push_back({{r,f},{row,2}});
        }
    }
    return legal;
}

bool hasAnyLegalMove(const GameState& gs, Color c) {
    for(int r=0;r<8;r++) for(int f=0;f<8;f++){
        if(gs.board[r][f].color != c) continue;
        GameState tmp = gs; tmp.turn = c;
        if(!getLegalMoves(tmp,r,f).empty()) return true;
    }
    return false;
}

// ─── Apply Move (full, with state updates) ───────────────────────────────────

bool applyMove(GameState& gs, const Move& m) {
    int r=m.from.r, f=m.from.f, nr=m.to.r, nf=m.to.f;
    Piece p = gs.board[r][f];
    Color c = p.color;

    // Validate: is this move in the legal list?
    auto legal = getLegalMoves(gs, r, f);
    bool found = false;
    for(auto& lm : legal)
        if(lm.to.r==nr && lm.to.f==nf){ found=true; break; }
    if(!found) return false;

    // Capture
    Piece captured = gs.board[nr][nf];

    // En passant
    gs.epRank = gs.epFile = -1;
    if(p.type==PAWN && abs(nr-r)==2){
        gs.epRank = (r+nr)/2;
        gs.epFile = f;
    }

    // En passant capture
    if(p.type==PAWN && nf!=f && gs.board[nr][nf].type==EMPTY){
        captured = gs.board[r][nf];
        gs.board[r][nf] = EMPTY_PIECE;
    }

    // Castling rook move
    if(p.type==KING){
        if(c==WHITE){ gs.castleWK=gs.castleWQ=false; }
        else         { gs.castleBK=gs.castleBQ=false; }
        int row = nr;
        if(nf==6 && f==4){ gs.board[row][5]=gs.board[row][7]; gs.board[row][7]=EMPTY_PIECE; }
        if(nf==2 && f==4){ gs.board[row][3]=gs.board[row][0]; gs.board[row][0]=EMPTY_PIECE; }
    }
    if(p.type==ROOK){
        if(c==WHITE){ if(f==0) gs.castleWQ=false; if(f==7) gs.castleWK=false; }
        else         { if(f==0) gs.castleBQ=false; if(f==7) gs.castleBK=false; }
    }

    gs.board[nr][nf] = p;
    gs.board[r][f]   = EMPTY_PIECE;

    // Pawn promotion
    if(p.type==PAWN && (nr==0||nr==7))
        gs.board[nr][nf] = {c, QUEEN};

    // Record capture
    if(captured.type != EMPTY){
        if(c==WHITE) gs.capturedByWhite.push_back(captured);
        else         gs.capturedByBlack.push_back(captured);
    }

    gs.turn = opp(c);
    return true;
}

// ─── Display ──────────────────────────────────────────────────────────────────

void printCaptured(const std::vector<Piece>& pieces, const std::string& label){
    std::cout << label << ": ";
    for(auto& p : pieces) std::cout << pieceSymbol(p) << " ";
    std::cout << "\n";
}

void printBoard(const GameState& gs){
    std::cout << "\n";
    printCaptured(gs.capturedByWhite, "White captured");
    std::cout << "\n";
    std::cout << "    a  b  c  d  e  f  g  h\n";
    std::cout << "  ┌─────────────────────────┐\n";
    for(int r=0;r<8;r++){
        std::cout << (8-r) << " │";
        for(int f=0;f<8;f++){
            std::cout << " " << pieceSymbol(gs.board[r][f]) << " ";
        }
        std::cout << "│ " << (8-r) << "\n";
    }
    std::cout << "  └─────────────────────────┘\n";
    std::cout << "    a  b  c  d  e  f  g  h\n\n";
    printCaptured(gs.capturedByBlack, "Black captured");
    std::cout << "\n";
}

// ─── Input Parsing ────────────────────────────────────────────────────────────

bool parseInput(const std::string& input, Move& move){
    // Expects format like "e2e4" or "e2 e4"
    std::string s;
    for(char ch : input) if(ch!=' ' && ch!='\n') s+=ch;
    if(s.size()<4) return false;
    int fc = s[0]-'a', fr = 8-(s[1]-'0');
    int tc = s[2]-'a', tr = 8-(s[3]-'0');
    if(!inBounds(fr,fc)||!inBounds(tr,tc)) return false;
    move = {{fr,fc},{tr,tc}};
    return true;
}

// ─── Main Game Loop ───────────────────────────────────────────────────────────

int main(){
    std::cout << "╔══════════════════════════════════╗\n";
    std::cout << "║     Two-Player Chess in C++      ║\n";
    std::cout << "║  Move format: e2e4  (from→to)   ║\n";
    std::cout << "║  Castling:    e1g1 / e1c1        ║\n";
    std::cout << "║  Type 'quit' to exit             ║\n";
    std::cout << "╚══════════════════════════════════╝\n";

    GameState gs;
    initBoard(gs);

    while(true){
        printBoard(gs);

        Color c = gs.turn;
        std::string colorName = (c==WHITE) ? "White" : "Black";

        if(!hasAnyLegalMove(gs, c)){
            if(inCheck(gs, c))
                std::cout << "♛ CHECKMATE! " << (c==WHITE?"Black":"White") << " wins!\n";
            else
                std::cout << "½ STALEMATE — Draw!\n";
            break;
        }

        if(inCheck(gs, c))
            std::cout << "⚠  " << colorName << " is in CHECK!\n";

        std::cout << colorName << "'s move: ";
        std::string input;
        std::getline(std::cin, input);

        if(input=="quit"||input=="exit") break;

        Move m;
        if(!parseInput(input, m)){
            std::cout << "Invalid format. Use e.g. e2e4\n";
            continue;
        }

        if(!applyMove(gs, m)){
            std::cout << "Illegal move. Try again.\n";
        }
    }

    std::cout << "\nGame over. Thanks for playing!\n";
    return 0;
}
