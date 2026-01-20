import os
import random
import string
import csv

def generate_unique_words(total_needed):
    """Generates a set of unique 8-character lowercase strings."""
    unique_words = set()
    while len(unique_words) < total_needed:
        # Generate a random 8-character string
        new_word = ''.join(random.choices(string.ascii_lowercase, k=8))
        unique_words.add(new_word)
    return list(unique_words)

def create_test_files(target_folder, num_files, words_per_file, csv_path):
    if not os.path.exists(target_folder):
        os.makedirs(target_folder)

    # 1. Calculate total words needed and generate them
    total_words_needed = num_files * words_per_file
    print(f"Generating {total_words_needed} unique words...")
    all_unique_words = generate_unique_words(total_words_needed)

    # 2. Distribute words into file structures (in memory)
    # files_data is a list of lists: [[word1, word2, ...], [word1, word2, ...]]
    files_data = []
    for i in range(num_files):
        start_idx = i * words_per_file
        end_idx = start_idx + words_per_file
        files_data.append(all_unique_words[start_idx:end_idx])

    # 3. Read CSV and inject sentences
    # Expected CSV format: Sentence, Count
    if os.path.exists(csv_path):
        print(f"Processing CSV: {csv_path}")
        with open(csv_path, mode='r', encoding='utf-8') as f:
            reader = csv.reader(f)
            for row in reader:
                if not row: continue
                sentence = row[0]
                try:
                    target_count = int(row[1])
                except (ValueError, IndexError):
                    target_count = 1 # Default if count is missing or invalid

                # Pick unique random files to host this sentence
                # Ensure we don't try to pick more files than exist
                actual_targets = min(target_count, num_files)
                chosen_file_indices = random.sample(range(num_files), actual_targets)

                for idx in chosen_file_indices:
                    # Insert sentence at a random position in the word list
                    # This ensures sentences are treated as discrete units and don't "overlap"
                    insert_pos = random.randint(0, len(files_data[idx]))
                    files_data[idx].insert(insert_pos, sentence)

    # 4. Write data to physical files
    print(f"Writing {num_files} files to '{target_folder}'...")
    for i, content_list in enumerate(files_data):
        file_name = f"test_file_{i+1}.txt"
        file_path = os.path.join(target_folder, file_name)
        with open(file_path, 'w', encoding='utf-8') as f:
            # Joining with spaces to form a "document"
            f.write(' '.join(content_list))

    print("Task completed successfully.")

# --- Configuration ---
TARGET_DIRECTORY = "./tests/generated"
NUMBER_OF_FILES = 100000
WORDS_PER_FILE = 500
CSV_FILE_PATH = "generate_test_dataset.csv" # Format: "The quick brown fox", 3

if __name__ == "__main__":
    create_test_files(TARGET_DIRECTORY, NUMBER_OF_FILES, WORDS_PER_FILE, CSV_FILE_PATH)
