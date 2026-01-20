import pandas as pd
from tqdm import tqdm
import json
import numpy as np
import re

def normalize_phrase(p):
    """Normalize whitespace and case for consistent lookups."""
    return " ".join(str(p).lower().strip().split())

def build_phrase_tree(df):
    """
    Standard tree building logic.
    Finds the 'longest existing' parent for each phrase (prefix or suffix).
    """
    df['phrase'] = df['phrase'].apply(normalize_phrase)
    df = df.sort_values('length').reset_index(drop=True)
    df['id'] = df.index
    df['parent_id'] = None
    df['level'] = 0

    phrase_to_id = {row.phrase: i for i, row in df.iterrows()}

    print("Linking Parents...")
    for i in tqdm(range(len(df)), desc="Linking"):
        words = df.iloc[i].phrase.split()
        l = len(words)
        found = False

        # Search for the longest available parent in the dataset
        for drop in range(1, l - 1):
            suffix = " ".join(words[drop:])
            prefix = " ".join(words[:-drop])

            for potential_parent in [suffix, prefix]:
                if potential_parent in phrase_to_id:
                    p_idx = phrase_to_id[potential_parent]
                    df.at[i, 'parent_id'] = int(p_idx)
                    df.at[i, 'level'] = int(df.at[p_idx, 'level']) + 1
                    found = True
                    break
            if found: break

    return df

def compress_unbranched_branches(nodes, parent_phrase=None):
    """
    Recursive function to collapse 'ladders'.
    If a node leads to exactly one child, it skips to the end of that
    chain (the longest variant) or to the first branching point.
    """
    compressed = []

    for node in nodes:
        curr = node

        # 1. Skip nodes that have exactly one child (the 'ladder')
        # This moves 'curr' to the longest variant in the unbranched path
        while len(curr['children']) == 1:
            curr = curr['children'][0]

        # 2. Update the display phrase for the new representative node
        # It must be relative to the parent ABOVE the entire collapsed chain
        if parent_phrase and parent_phrase in curr['phrase']:
            # Replace the parent part with <PARENT>
            display = curr['phrase'].replace(parent_phrase, " <PARENT> ")
            curr['display_phrase'] = " ".join(display.split())
        else:
            # If it's a root node, show the full phrase
            curr['display_phrase'] = curr['phrase']

        # 3. Recursively process children of the promoted node
        if curr['children']:
            # The new 'curr' is now the parent for its children
            curr['children'] = compress_unbranched_branches(curr['children'], curr['phrase'])

        compressed.append(curr)

    return compressed

def generate_html_tree(df, output_file="tree_view.html", max_nodes=15000):
    """Generates a collapsible HTML tree view with compression logic applied."""

    if 'score' not in df.columns:
        max_l, max_f = df['length'].max(), df['freq'].max()
        df['score'] = np.sqrt((1 - df['length']/max_l)**2 + (1 - np.log1p(df['freq'])/np.log1p(max_f))**2)
    # 1. Select top nodes and ensure parent integrity (all ancestors included)
    top_df = df.sort_values('score').head(max_nodes)
    all_visible_ids = set(top_df['id'].tolist())

    for _, row in top_df.iterrows():
        curr_p = row['parent_id']
        while curr_p is not None:
            p_val = int(curr_p)
            if p_val in all_visible_ids: break
            all_visible_ids.add(p_val)
            curr_p = df.at[p_val, 'parent_id']

    final_viz_df = df[df['id'].isin(all_visible_ids)].copy()

    # 2. Build the JSON Tree structure
    nodes_dict = final_viz_df.to_dict('records')
    id_map = {int(n['id']): n for n in nodes_dict}
    for n in nodes_dict: n['children'] = []

    raw_tree = []
    for n in nodes_dict:
        p_id = n.get('parent_id')
        if p_id is None or int(p_id) not in id_map:
            raw_tree.append(n)
        else:
            id_map[int(p_id)]['children'].append(n)

    # 3. Apply the compression logic to remove unbranched chains
    print("Compressing unbranched paths...")
    final_tree = compress_unbranched_branches(raw_tree)

    # 4. Generate the HTML (Template remains standard)
    html_template = """
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <title>Phrase Discovery Tree</title>
        <style>
            body { font-family: -apple-system, system-ui, sans-serif; background: #f4f4f9; padding: 40px; }
            .tree-container { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
            ul { list-style-type: none; margin-left: 30px; border-left: 2px solid #edf0f2; padding-left: 15px; }
            .node { cursor: pointer; display: flex; align-items: center; padding: 8px; border-bottom: 1px solid #f0f0f0; }
            .node:hover { background: #f8fbff; }
            .meta { font-family: monospace; background: #333; color: white; padding: 2px 8px; border-radius: 4px; font-size: 0.75em; margin-right: 15px; }
            .phrase { font-family: "SFMono-Regular", Consolas, monospace; font-size: 0.9em; }
            .parent-tag { color: #007bff; font-weight: bold; background: #e7f3ff; padding: 0 4px; border-radius: 3px; }
            .toggle { margin-right: 10px; font-size: 12px; color: #999; width: 15px; }
            .hidden { display: none; }
            .collapsed .toggle::before { content: "▶"; }
            .expanded .toggle::before { content: "▼"; }
            .leaf .toggle::before { content: "•"; color: #ccc; }
        </style>
    </head>
    <body>
        <h2>Sequential Phrase Discovery Tree (Compressed)</h2>
        <div class="tree-container" id="tree"></div>
        <script>
            const data = %DATA%;
            function createNode(n) {
                const li = document.createElement('li');
                const div = document.createElement('div');
                div.className = 'node';
                const hasChildren = n.children && n.children.length > 0;
                div.classList.add(hasChildren ? 'collapsed' : 'leaf');
                
                const display = n.display_phrase.replace(/<PARENT>/g, '<span class="parent-tag">&lt;PARENT&gt;</span>');
                div.innerHTML = `<span class="toggle"></span><span class="meta">F:${n.freq} L:${n.length}</span><span class="phrase">${display}</span>`;
                
                li.appendChild(div);
                if (hasChildren) {
                    const ul = document.createElement('ul');
                    ul.className = 'hidden';
                    n.children.sort((a,b) => b.freq - a.freq).forEach(c => ul.appendChild(createNode(c)));
                    div.onclick = (e) => {
                        const isHidden = ul.classList.contains('hidden');
                        ul.classList.toggle('hidden');
                        div.classList.toggle('collapsed', !isHidden);
                        div.classList.toggle('expanded', isHidden);
                        e.stopPropagation();
                    };
                    li.appendChild(ul);
                }
                return li;
            }
            const root = document.createElement('ul');
            data.sort((a,b) => b.freq - a.freq).forEach(d => root.appendChild(createNode(d)));
            document.getElementById('tree').appendChild(root);
        </script>
    </body>
    </html>
    """
    with open(output_file, "w", encoding="utf-8") as f:
        f.write(html_template.replace("%DATA%", json.dumps(final_tree, ensure_ascii=False)))