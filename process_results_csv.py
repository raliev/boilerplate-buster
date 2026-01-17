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

    print(f"Reading {args.input}...")
    try:
        df = pd.read_csv(args.input)
    except Exception as e:
        print(f"Error: {e}")
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
    tree_df.to_csv(args.output, index=False)

    generate_html_tree(tree_df, "visualization.html", max_nodes=args.limit)
    print("\nProcess finished successfully.")
    print("Check 'visualization.html' to browse the phrase tree.")

if __name__ == "__main__":
    main()