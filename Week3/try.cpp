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

int piece_value(const chess::Piece &pc)
{
    using U = chess::PieceType::underlying;
    U pt = pc.type().internal();

    switch (pt)
    {
    case U::PAWN:
        return 100;
    case U::KNIGHT:
        return 300;
    case U::BISHOP:
        return 320;
    case U::ROOK:
        return 500;
    case U::QUEEN:
        return 900;
    case U::KING:
        return 10000;
    default:
        return 0;
    }
}
pair<bool, bool> isOver(Board &board, unordered_map<uint64_t, pair<bool, bool>> &gameOver)
{
    uint64_t key = board.hash();
    if (gameOver.find(key) != gameOver.end())
        return gameOver[key];
    auto [reason, result] = board.isGameOver();
    if (reason != GameResultReason::NONE)
    {
        if (reason == GameResultReason::CHECKMATE)
        {
            gameOver[key] = {true, 1};
            return {true, 1};
        }
        else
        {
            gameOver[key] = {true, 0};
            return {true, 0};
        }
    }
    return {false, 0};
}
std::vector<chess::Move> order_moves(chess::Board &board_now, const chess::Movelist &moves, unordered_map<uint64_t, pair<bool, bool>> &gameOver, const int depth)
{
    vector<pair<int, Move>> scored;
    scored.reserve(moves.size());
    Color enemy = ~board_now.sideToMove();
    Square enemy_king_sq = board_now.kingSq(enemy);
    for (const Move &m : moves)
    {
        int score = 0;

        // 1. Check bonus
        if (board_now.givesCheck(m) != chess::CheckType::NO_CHECK)
        {
            score += 10000;
            // if (depth == 1)
            // {
            //     board_now.makeMove(m);
            //     auto it = isOver(board_now, gameOver);
            //     board_now.unmakeMove(m);
            //     if (it.first)
            //     {
            //         vector<Move> ordered;
            //         ordered.push_back(m);
            //         return ordered;
            //     }
            // }
            // board_now.makeMove(m);
            // auto [reason, result] = board_now.isGameOver();
            // if (reason != GameResultReason::NONE)
            // {
            //     if (reason == GameResultReason::CHECKMATE)
            //     {
            //         vector<Move> ordered;
            //         ordered.push_back(m);
            //         board_now.unmakeMove(m);
            //         return ordered;
            //     }
            // }
            // board_now.unmakeMove(m);
        }

        // 2. Promotion bonus
        if (m.typeOf() == Move::PROMOTION)
            score += 10000;

        if (board_now.isCapture(m))
        {
            Piece victim = board_now.at(m.to());
            // score += 10 * piece_value(victim);
            score += 2000;
        }
        // PieceType from_pt = board_now.at(m.from()).type();
        // score += 2*piece_value(board_now.at(m.from()));

        // int to_file = static_cast<int>(m.to().file());
        // int to_rank = static_cast<int>(m.to().rank());
        // if (to_file >= 2 && to_file <= 5 && to_rank >= 2 && to_rank <= 5)
        //     score += 200;
        // 5. Proximity to king bonus if (found_king)
        {
            // int from_rank = static_cast<int>(m.from().rank());
            // int from_file = static_cast<int>(m.from().file());
            // int to_rank = static_cast<int>(m.to().rank());
            // int to_file = static_cast<int>(m.to().file());
            // int king_rank = static_cast<int>(enemy_king_sq.rank());
            // int king_file = static_cast<int>(enemy_king_sq.file());

            // int dist_before = std::abs(from_rank - king_rank) + std::abs(from_file - king_file);
            // int dist_after = std::abs(to_rank - king_rank) + std::abs(to_file - king_file);
            int dist_before = Square::distance(m.from(), enemy_king_sq);
            int dist_after = Square::distance(m.to(), enemy_king_sq);
            score += (dist_before - dist_after) * 50;
        }
        scored.emplace_back(score, m);
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto &a, const auto &b)
              { return a.first > b.first; });

    vector<Move> ordered;
    ordered.reserve(scored.size());
    for (auto &[_, m] : scored)
        ordered.push_back(m);

    return ordered;
}

int eval(Board &board, int depth, int p, unordered_map<uint64_t, Move> &mp, unordered_map<uint64_t, pair<bool, bool>> &gameOver)
{
    uint64_t key = board.hash();
    Movelist moves;
    movegen::legalmoves(moves, board);
    auto it = isOver(board, gameOver);
    if (it.first)
    {
        return it.second;
    }
    if (board.sideToMove() == (p == 1 ? Color::WHITE : Color::BLACK))
    {
        auto ordered = order_moves(board, moves, gameOver,depth);
        int mx = 0;
        for (const auto &move : ordered)
        {

            board.makeMove(move);
            int val = 1;
            if (depth == 1)
            {
                auto it = isOver(board, gameOver);
                val = it.second;
            }
            else
            {
                val = eval(board, depth - 1, p, mp, gameOver);
            }
            mx = max(mx, val);
            board.unmakeMove(move);
            if (mx == 1)
            {
                mp[key] = move;
                break;
            }
        }
        return mx;
    }
    else
    {
        int mn = 1;
        for (const auto &move : moves)
        {
            board.makeMove(move);
            int val = 0;
            if (depth == 1)
            {
                auto it = isOver(board, gameOver);
                val = it.second;
            }
            else
            {
                val = eval(board, depth - 1, p, mp, gameOver);
            }
            mn = min(mn, val);
            board.unmakeMove(move);
            if (mn == 0)
                break;
        }
        if (mn == 1)
        {
            mp[key] = moves[0];
        }
        return mn;
    }
}

pair<bool, string> solve_single_puzzle(const string &fen)
{
    Board board;
    board.setFen(fen);

    unordered_map<uint64_t, Move> mp;
    mp.reserve(10000);
    unordered_map<uint64_t, pair<bool, bool>> gameOver;
    gameOver.reserve(10000);
    int val = eval(board, 6, board.sideToMove() == Color::WHITE ? 1 : 0, mp, gameOver);
    string sol;
    for (int i = 0; i < 5; i++)
    {

        auto it = isOver(board, gameOver);
        if (it.first)
        {
            break;
        }
        uint64_t key = board.hash();
        if (mp.find(key) != mp.end())
        {
            auto move = mp[key];
            sol += uci::moveToSan(board, move) + " ";
            board.makeMove(move);
        }
    }
    return {val, sol};
}
#include <string>
#include <regex>
#include <algorithm>

std::string getFirstMove(const std::string &solution)
{
    // Remove leading/trailing and extra spaces
    std::string clean = std::regex_replace(solution, std::regex(R"(\s+)"), " ");
    clean.erase(0, clean.find_first_not_of(' '));
    clean.erase(clean.find_last_not_of(' ') + 1);

    // Match either: number + optional dots + space + move
    // or: directly the move at the beginning
    std::regex move_regex(R"((?:\d+\.*\.*\s*)?([a-hNBRQKO][a-h1-8x\-=\+#]*))");
    std::smatch match;
    if (std::regex_search(clean, match, move_regex))
    {
        std::string move = match[1];

        // Normalize: remove =Q/R/B/N, +, # etc.
        move = std::regex_replace(move, std::regex(R"(=[QRBN ]*)"), ""); // remove promotion
        move = std::regex_replace(move, std::regex(R"([\+#])"), "");     // remove check or mate symbols
        return move;
    }
    return "";
}
// int main()
// {
//     using namespace chrono;
//     auto start_time = high_resolution_clock::now();

//     ifstream file("mate_in_3.json");
//     if (!file.is_open())
//     {
//         cerr << "Failed to open mate_in_3.json\n";
//         return 1;
//     }

//     json j;
//     file >> j;
//     file.close();

//     const int MAX_CONCURRENT = 1;
//     int correct = 0;

//     // Prepare problems queue and mappings
//     queue<pair<string, string>> taskQueue;

//     for (auto &[fen, solution] : j.items())
//     {
//         taskQueue.emplace(fen, solution);
//     }

//     vector<pair<string, future<pair<bool, string>>>> futures;

//     while (!taskQueue.empty() || !futures.empty())
//     {
//         // Launch new tasks if under the limit
//         while (futures.size() < MAX_CONCURRENT && !taskQueue.empty())
//         {
//             auto [fen, solution] = taskQueue.front();
//             taskQueue.pop();
//             futures.emplace_back(solution, async(launch::async, solve_single_puzzle, fen));
//         }

//         // Check for completed tasks
//         for (auto it = futures.begin(); it != futures.end();)
//         {
//             auto &[solution, fut] = *it;
//             if (fut.wait_for(chrono::milliseconds(0)) == future_status::ready)
//             {
//                 auto result = fut.get(); // {success, solution string}
//                 if (result.first)
//                 {
//                     string expected_move = getFirstMove(solution);
//                     string actual_move = getFirstMove(result.second);

//                     if (expected_move == actual_move)
//                     {
//                         correct++;
//                         cout << correct << ". ✅ Matched first move: " << actual_move << endl;
//                     }
//                     else
//                     {
//                         cout << "❌ Mismatch";
//                         cout << "   Expected: " << expected_move << "\n";
//                         cout << "   Got     : " << actual_move << "\n";
//                     }
//                 }
//                 it = futures.erase(it);
//             }
//             else
//             {
//                 ++it;
//             }
//         }

//         this_thread::sleep_for(chrono::milliseconds(1));
//     }

//     auto end_time = high_resolution_clock::now();
//     auto duration = duration_cast<milliseconds>(end_time - start_time).count();
//     cout << "\nTotal correctly matched first moves: " << correct << "\n";
//     cout << "Time taken: " << (duration / 1000.0) << " seconds\n";

//     return 0;
// }
int main()
{
    using namespace chrono;
    auto start_time = high_resolution_clock::now();

    ifstream file("mate_in_3.json");
    if (!file.is_open())
    {
        cerr << "Failed to open mate_in_3.json\n";
        return 1;
    }

    json j;
    file >> j;
    file.close();

    // const int MAX_CONCURRENT = 5;
    // vector<future<pair<bool, string>>> futures;
    // futures.reserve(j.size());

    // vector<pair<string, string>> problems;
    // for (auto &[fen, solution] : j.items())
    // {
    //     problems.emplace_back(fen, solution);
    // }

    // int correct = 0;
    // for (size_t i = 0; i < problems.size(); i += MAX_CONCURRENT)
    // {
    //     vector<future<pair<bool, string>>> batch;

    //     for (int jdx = 0; jdx < MAX_CONCURRENT && i + jdx < problems.size(); ++jdx)
    //     {
    //         batch.push_back(async(launch::async, solve_single_puzzle, problems[i + jdx].first));
    //     }

    //     for (auto &f : batch)
    //     {
    //         auto result = f.get();
    //         if (result.first)
    //         {
    //             correct++;
    //             cout << correct << ". Solution found: " << result.second << endl;
    //         }
    //     }
    // }
    int correct = 0;

    for (auto &[fen, solution] : j.items())
    {
        auto result = solve_single_puzzle(fen);
        // cout<< getFirstMove(result.second) << " " <<getFirstMove(solution)<<endl;
        if (result.first && (getFirstMove(result.second) == getFirstMove(solution)))
        {
            correct++;
            cout << correct << ". Solution found: " << result.second << " Expected Solution " << solution << endl;
        }
    }
    auto end_time = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end_time - start_time);
    cout << "Mate in 4 puzzles solver\n";
    cout << "=========================\n";
    cout << "Solved " << correct << "/" << j.size() << " correctly.\n";
    cout << "Time taken: " << duration.count() / 1000.0 << " s\n";

    return 0;
}
