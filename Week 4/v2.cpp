#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <memory>

// TensorFlow C++ API headers
#include "tensorflow/c/c_api.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/cc/saved_model/loader.h"
#include "tensorflow/cc/saved_model/tag_constants.h"

#include "chess.hpp"

using namespace std;
using namespace chess;
using namespace tensorflow;

// --- Global Variables ---
int maxDepth = 4; // Use a more reasonable depth for testing
Move pv_move;

// TensorFlow Model Globals
unique_ptr<SavedModelBundle> model_bundle;
unique_ptr<ClientSession> session;

// --- Helper Functions ---

// ⚠️ YOU MUST IMPLEMENT THIS YOURSELF! ⚠️
// This function must convert a board position into the tensor format your model expects.
// The example below assumes a (1, 8, 8, 12) tensor for piece planes.
// Adjust shape and logic to match your model's input layer.
Tensor fenToInputTensor(const Board &board)
{
    // Example for a model expecting a 1x8x8x12 tensor
    // (12 planes for 6 piece types x 2 colors)
    Tensor input_tensor(DT_FLOAT, TensorShape({1, 8, 8, 12}));
    auto tensor_map = input_tensor.tensor<float, 4>();

    // Zero out the tensor
    for (int i = 0; i < 8; ++i)
    {
        for (int j = 0; j < 8; ++j)
        {
            for (int k = 0; k < 12; ++k)
            {
                tensor_map(0, i, j, k) = 0.0f;
            }
        }
    }

    // Populate the tensor based on piece positions
    // This logic needs to be carefully implemented to match your Python preprocessing
    for (int p_type_idx = 0; p_type_idx < 6; ++p_type_idx)
    {
        PieceType p_type = static_cast<PieceType>(p_type_idx);

        // White pieces (channel p_type_idx)
        Bitboard white_pieces = board.pieces(p_type, Color::WHITE);
        for (const auto &sq : white_pieces)
        {
            tensor_map(0, sq.rank(), sq.file(), p_type_idx) = 1.0f;
        }

        // Black pieces (channel p_type_idx + 6)
        Bitboard black_pieces = board.pieces(p_type, Color::BLACK);
        for (const auto &sq : black_pieces)
        {
            tensor_map(0, sq.rank(), sq.file(), p_type_idx + 6) = 1.0f;
        }
    }

    return input_tensor;
}

// --- Model and Evaluation ---

void loadModel()
{
    model_bundle = make_unique<SavedModelBundle>();
    SessionOptions session_options;
    RunOptions run_options;

    // IMPORTANT: Replace "saved_model_directory" with the path to your exported model
    Status status = LoadSavedModel(session_options, run_options, "saved_model_directory", {kSavedModelTagServe}, model_bundle.get());

    if (!status.ok())
    {
        cerr << "Error loading model: " << status.ToString() << endl;
        exit(1);
    }
    cout << "info string Model loaded successfully." << endl;
}

// Evaluates a board position using the loaded Neural Network
float evaluateWithNN(Board &board)
{
    // 1. Convert board state to a tensor
    Tensor input_tensor = fenToInputTensor(board);

    // 2. Run the model
    vector<Tensor> outputs;
    Status status = model_bundle->session->Run({{"serving_default_input_1:0", input_tensor}}, {"StatefulPartitionedCall:0"}, {}, &outputs);
    // Note: The input/output tensor names ("serving_default_input_1:0", etc.)
    // might be different. You can inspect them using the `saved_model_cli` tool.
    // `saved_model_cli show --dir saved_model_directory --all`

    if (!status.ok())
    {
        cerr << "Error running inference: " << status.ToString() << endl;
        return 0.0f;
    }

    // 3. Extract the evaluation score from the output tensor
    float eval_score = outputs[0].scalar<float>()();

    // The model typically gives a score from White's perspective.
    // Adjust if it's the opponent's turn.
    return (board.sideToMove() == Color::WHITE) ? eval_score : -eval_score;
}

// --- Search Algorithm ---

float alphabeta(Board &board, int depth, float alpha, float beta, bool maximizing)
{
    auto [is_over, result] = board.isGameOver();
    if (is_over)
    {
        if (result == GameResult::CHECKMATE)
        {
            return maximizing ? -99999.0f : 99999.0f; // Punish the side that got checkmated
        }
        return 0.0f; // Draw
    }

    if (depth == 0)
    {
        return evaluateWithNN(board);
    }

    Movelist moves;
    movegen::legalmoves(moves, board);

    if (maximizing)
    {
        float maxEval = -numeric_limits<float>::infinity();
        for (const Move &move : moves)
        {
            board.makeMove(move);
            float eval = alphabeta(board, depth - 1, alpha, beta, false);
            board.unmakeMove(move);
            if (eval > maxEval)
            {
                maxEval = eval;
                if (depth == maxDepth)
                { // Store principal variation move at the root
                    pv_move = move;
                }
            }
            alpha = max(alpha, eval);
            if (beta <= alpha)
            {
                break;
            }
        }
        return maxEval;
    }
    else
    {
        float minEval = numeric_limits<float>::infinity();
        for (const Move &move : moves)
        {
            board.makeMove(move);
            float eval = alphabeta(board, depth - 1, alpha, beta, true);
            board.unmakeMove(move);
            minEval = min(minEval, eval);
            beta = min(beta, eval);
            if (beta <= alpha)
            {
                break;
            }
        }
        return minEval;
    }
}

// --- UCI Main Loop ---

int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Load the model at startup
    loadModel();

    Board board;
    string line;

    while (getline(cin, line))
    {
        istringstream iss(line);
        string token;
        iss >> token;

        if (token == "uci")
        {
            cout << "id name NNChessEngine\n";
            cout << "id author You\n";
            cout << "uciok\n";
        }
        else if (token == "isready")
        {
            cout << "readyok\n";
        }
        else if (token == "ucinewgame")
        {
            board.setFen(constants::STARTPOS);
        }
        else if (token == "position")
        {
            string sub_token;
            iss >> sub_token;
            if (sub_token == "startpos")
            {
                board.setFen(constants::STARTPOS);
                string moves_token;
                if (iss >> moves_token && moves_token == "moves")
                {
                    string move_str;
                    while (iss >> move_str)
                    {
                        board.makeMove(uci::uciToMove(board, move_str));
                    }
                }
            }
            else if (sub_token == "fen")
            {
                string fen_str;
                string part;
                while (iss >> part)
                {
                    fen_str += part + " ";
                }
                board.setFen(fen_str);
            }
        }
        else if (token == "go")
        {
            bool isMaximizing = (board.sideToMove() == Color::WHITE);
            alphabeta(board, maxDepth, -numeric_limits<float>::infinity(), numeric_limits<float>::infinity(), isMaximizing);
            cout << "bestmove " << uci::moveToUci(pv_move) << endl;
        }
        else if (token == "quit")
        {
            break;
        }
    }

    return 0;
}