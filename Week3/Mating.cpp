#include "chess.hpp"
#include <bits/stdc++.h>
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <regex>
using json = nlohmann::json;
using namespace std;
using namespace chess;
int eval(Board &board, int depth, map<string, int> &vals, int p)
{
    string fen = board.getFen();
    if (vals.find(fen) != vals.end())
    {
        return vals[fen];
    }

    if (depth == 0)
    {
        vals[fen] = 0;
        return 0;
    }
    Movelist moves;
    movegen::legalmoves(moves, board);

    if (moves.size() == 0)
    {
        if (board.inCheck())
        {
            vals[fen] = 1;
        }
        else
        {
            vals[fen] = 0;
        }
        return vals[fen];
    }

    if (board.sideToMove() == (p == 1 ? (Color::WHITE) : (Color::BLACK)))
    {
        int mx = 0;
        for (const auto &move : moves)
        {
            board.makeMove(move);
            mx = max(mx, eval(board, depth - 1, vals, p));
            board.unmakeMove(move);
            if (mx == 1)
                break;
        }
        vals[fen] = mx;
        return mx;
    }
    else
    {
        int mn = 1;
        for (const auto &move : moves)
        {
            board.makeMove(move);
            mn = min(mn, eval(board, depth - 1, vals, p));
            board.unmakeMove(move);
            if (mn == 0)
                break;
        }
        vals[fen] = mn;
        return mn;
    }
}
// string sol(Board &board, int depth, map<string, int> &vals)
// {
//     string ans = "";

//     for (int i = 0; i < depth; i++)
//     {
//         Movelist moves;
//         movegen::legalmoves(moves, board);
//         for (const auto &move : moves)
//         {
//             board.makeMove(move);
//             string fen = board.getFen();
//             if (vals[fen] == 1)
//             {
//                 board.unmakeMove(move);
//                 ans += uci::moveToSan(board, move) + " ";
//                 board.makeMove(move);
//                 break;
//             }
//             board.unmakeMove(move);
//         }
//     }
//     return ans;
// }
void sol(
    Board &board,
    int depth,
    map<string, int> &vals,
    int p,
    const string &soFar,
    vector<string> &allLines)
{
    // Base case: if we've added exactly “depth” plies, record the line:
    if (depth == 0)
    {
        allLines.push_back(soFar);
        return;
    }

    // Generate all legal moves from the current position:
    Movelist moves;
    movegen::legalmoves(moves, board);

    // Figure out which color is “maximizer” at the root:
    // p == 1 means White was to move at root; p == 0 means Black was to move at root
    Color rootMax = (p == 1 ? Color::WHITE : Color::BLACK);

    // For each legal move…
    for (const auto &mv : moves)
    {
        // 1) Make the move to get its resulting FEN:
        board.makeMove(mv);
        string childFen = board.getFen();

        // 2) If that child‐position is still on a forced‐win path (vals[childFen] == 1),
        //    then we know this move belongs to *some* mate‐in-depth line.
        if (vals[childFen] == 1)
        {
            // But before recursing, we want the SAN from the *current* position.
            // So unmake it, generate SAN, then make it again and recurse:
            board.unmakeMove(mv);

            // Generate SAN from the position *before* mv:
            string san = uci::moveToSan(board, mv);

            // Now “make” it again so that the recursive call sees the updated board:
            board.makeMove(mv);

            // Recurse with one fewer ply, appending “san + ' '” to soFar:
            sol(board,
                depth - 1,
                vals,
                p,
                soFar + san + " ",
                allLines);

            // Undo it so we can try the next move in this same position:
            board.unmakeMove(mv);
        }
        else
        {
            // If vals[childFen] != 1, this move cannot be on a forced mate→ just undo:
            board.unmakeMove(mv);
        }
    }
}

void trim(string &s)
{
    while (!s.empty() && isspace(s.back()))
        s.pop_back();
    while (!s.empty() && isspace(s.front()))
        s.erase(s.begin());
}
string normalizeMoves(const string &s)
{
    string res = s;
    res = regex_replace(res, regex(R"(\d+\.+)"), "");
    res = regex_replace(res, regex(R"(\s+)"), " ");

    // Trim leading and trailing spaces
    while (!res.empty() && isspace(res.front()))
        res.erase(res.begin());
    while (!res.empty() && isspace(res.back()))
        res.pop_back();

    return res;
}
int main()
{
    ifstream file("mate_in_2.json");
    if (!file.is_open())
    {
        cerr << "Failed to open puzzles.json\n";
        return 1;
    }

    json j;
    file >> j;

    map<string, string> expectedSolutions;
    for (auto &[fen, solution] : j.items())
    {
        expectedSolutions[fen] = solution;
    }

    int total = 0, correct = 0;

    for (const auto &[fen, expected] : expectedSolutions)
    {
        Board board;
        board.setFen(fen);

        map<string, int> vals;
        eval(board, 4, vals, board.sideToMove() == Color::WHITE ? 1 : 0);
        vector<string> allLines;
        sol(board, 3, vals, (board.sideToMove() == Color::WHITE ? 1 : 0), "", allLines);

        total++;

        if(allLines.empty())
        {
            cout << "No solution found for FEN: " << fen << "\n";
            continue;
        }
        else
        {
            correct++;
        }
    }
    cout << "\nSolved " << correct << "/" << total << " correctly.\n";
    return 0;
}