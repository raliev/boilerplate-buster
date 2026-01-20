import pandas as pd
import argparse
import sys
from tree_logic import build_phrase_tree, generate_html_tree

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="results_max.csv", help="Input CSV file")
    parser.add_argument("--output", default="results_tree.csv", help="Output CSV file with parent info")
    parser.add_argument("--limit", type=int, default=15000, help="Max nodes to include in HTML")
    # New parameters
    parser.add_argument("--min_l", type=int, default=0, help="Minimum phrase length")
    parser.add_argument("--min_f", type=int, default=0, help="Minimum frequency (doc count)")
    args = parser.parse_args()

    encodings_to_try = ['utf-8-sig', 'utf-16', 'utf-16le', 'utf-16be', 'latin-1']
    df = None

    for enc in encodings_to_try:
        try:
            # We use engine='python' occasionally for better error handling with encodings,
            # but the default C engine is usually fine if the encoding is correct.
            df = pd.read_csv(args.input, encoding=enc)
            print(f"Successfully loaded using {enc} encoding.")
            break
        except (UnicodeDecodeError, UnicodeError):
            continue
        except Exception as e:
            print(f"Skipping {enc} due to error: {e}")
            continue

    if df is None:
        print(f"Error: Could not decode {args.input} with any supported encoding.")
        sys.exit(1)

    # Sanitize data
    df = df.dropna(subset=['phrase'])
    if 'freq' not in df.columns and 'doc_count' in df.columns:
        df = df.rename(columns={'doc_count': 'freq'})
    if 'length' not in df.columns and 'word_count' in df.columns:
        df = df.rename(columns={'word_count': 'length'})

    if args.min_l > 0 or args.min_f > 0:
        initial_len = len(df)
        df = df[(df['length'] >= args.min_l) & (df['freq'] >= args.min_f)]
        print(f"Filtered {initial_len} down to {len(df)} phrases (Min L: {args.min_l}, Min F: {args.min_f})")

    if df.empty:
        print("No phrases match the specified criteria. Exiting.")
        return

    print(f"Building hierarchy for {len(df)} sequences...")
    tree_df = build_phrase_tree(df)

    print(f"Saving enriched CSV to {args.output}...")
    tree_df.to_csv(args.output, index=False, encoding='utf-8')

    generate_html_tree(tree_df, "visualization.html", max_nodes=args.limit)
    print("\nProcess finished successfully.")
    print("Check 'visualization.html' to browse the phrase tree.")

if __name__ == "__main__":
    main()