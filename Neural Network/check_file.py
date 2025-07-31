import os

# Use the exact path that is failing
file_path = r"C:\Users\aksha\Downloads\lichess-bot-master\lichess-bot-master\engines\chess_eval_resnet_trained.keras"

print(f"Checking for file at: {file_path}")

# Step 1: Check if the path exists according to the OS
if os.path.exists(file_path):
    print("✅ Success: os.path.exists() found the file.")

    # Step 2: Try to actually open the file for reading
    try:
        with open(file_path, 'rb') as f:
            print("✅ Success: The file was opened successfully.")
    except PermissionError:
        print("❌ Failure: A PermissionError occurred. The script cannot read the file.")
    except Exception as e:
        print(f"❌ Failure: An unexpected error occurred while opening the file: {e}")

else:
    print("❌ Failure: os.path.exists() could NOT find the file. Please double-check the path for typos.")