import tensorflow as tf

# --- Load your actual model ---
# Ensure 'chess_eval_resnet_trained.keras' is in the same folder as this script.
input_keras_path = 'chess_eval_resnet_trained.keras'
output_h5_path = 'chess_eval_resnet_trained.h5' # Use a matching output name

try:
    print(f"Loading model from: {input_keras_path}")
    model = tf.keras.models.load_model(input_keras_path)

    # --- Save the model in .h5 format ---
    # The 'save_format' argument is deprecated but still works.
    model.save(output_h5_path, save_format='h5')
    print(f"Model successfully converted and saved to: {output_h5_path}")

    # --- Verification (Optional) ---
    # Load the new .h5 model back with compile=False to ensure it works.
    print("Verifying the new .h5 file...")
    loaded_h5_model = tf.keras.models.load_model(output_h5_path, compile=False)
    loaded_h5_model.summary()
    print("Verification successful!")

except FileNotFoundError:
    print(f"ERROR: Could not find the model file at '{input_keras_path}'.")
    print("Please make sure the file exists and is in the correct directory.")
except Exception as e:
    print(f"An unexpected error occurred: {e}")