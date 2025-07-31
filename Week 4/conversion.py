import tensorflow as tf

# Load your existing Keras model
model = tf.keras.models.load_model('your_model.keras')

# Save it in the SavedModel format
# This creates a directory with 'saved_model.pb' and subdirectories
tf.saved_model.save(model, 'saved_model_directory')

print("Model successfully converted and saved to 'saved_model_directory'")