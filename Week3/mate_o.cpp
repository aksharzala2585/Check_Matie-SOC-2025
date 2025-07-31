#include "chess.hpp"
#include <bits/stdc++.h>
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <chrono>
#include <future>
using json = nlohmann::json;
using namespace std;
using namespace chess;

int eval(Board &board, int depth, unordered_map<uint64_t, int> &vals, int p)
{
    uint64_t key = board.zobrist();
    
    if (vals.find(key) != vals.end())
    {
        return vals[key];
    }

    Movelist moves;
    movegen::legalmoves(moves, board);

    if (moves.size() == 0)
    {
        if (board.inCheck())
        {
            vals[key] = 1;
        }
        else
        {
            vals[key] = 0;
        }
        return vals[key];
    }

    if (depth == 0)
    {
        vals[key] = 0;
        return 0;
    }

    if (board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK))
    {
        int mx = 0;
        for (const auto &move : moves)
        {
            board.makeMove(move);
            assert(board.hash() == board.zobrist());
            mx = max(mx, eval(board, depth - 1, vals, p));
            board.unmakeMove(move);
            if (mx == 1)
                break;
        }
        vals[key] = mx;
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
        vals[key] = mn;
        return mn;
    }
}

bool solve_single_puzzle(const string &fen)
{
    Board board;
    board.setFen(fen);
    unordered_map<uint64_t, int> vals;
    return eval(board, 4, vals, board.sideToMove() == Color::WHITE ? 1 : 0) == 1;
}

int main()
{
    using namespace chrono;
    auto start_time = high_resolution_clock::now();

    // string fen = "rnR5/p3p1kp/4p1pn/bpP5/5BP1/5N1P/2P2P2/2K5 w - - 1 0";
    // bool result = solve_single_puzzle(fen);
    // cout << "Puzzle: " << fen << "   " << result << "\n";
    // return 0;

    ifstream file("mate_in_2.json");
    if (!file.is_open())
    {
        cerr << "Failed to open mate_in_2.json\n";
        return 1;
    }

    json j;
    file >> j;
    file.close();

    vector<future<bool>> futures;
    futures.reserve(j.size());

    for (auto &[fen, solution] : j.items())
    {
        futures.push_back(async(launch::async, solve_single_puzzle, fen));
    }

    int correct = 0;
    for (auto &f : futures)
    {
        if (f.get())
            correct++;
    }

    auto end_time = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end_time - start_time);

    cout << "\nSolved " << correct << "/" << j.size() << " correctly.\n";
    cout << "Time taken: " << duration.count() << " ms\n";

    return 0;
}
