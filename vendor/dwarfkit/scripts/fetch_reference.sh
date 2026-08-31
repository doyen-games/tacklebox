#!/usr/bin/env bash
# Shallow-clones every wharfkit org repo (plus greymass/buoy-client) into reference/.
# reference/ is gitignored; re-run any time to pick up new repos or refresh.
set -uo pipefail
cd "$(dirname "$0")/.."
mkdir -p reference

list_repos() {
  if gh auth status >/dev/null 2>&1; then
    gh repo list wharfkit --limit 200 --json name -q '.[].name'
  else
    curl -fsSL "https://api.github.com/orgs/wharfkit/repos?per_page=100" \
      | python -c "import json,sys; [print(r['name']) for r in json.load(sys.stdin)]"
  fi
}

failures=0
for name in $(list_repos | sort); do
  dest="reference/$name"
  if [ -d "$dest/.git" ]; then
    echo "have  $name"
  else
    echo "clone $name"
    git clone --quiet --depth 1 "https://github.com/wharfkit/$name" "$dest" || { echo "FAIL  $name"; failures=$((failures+1)); }
  fi
done

# buoy-client lives in the greymass org
if [ ! -d reference/buoy-client/.git ]; then
  echo "clone buoy-client (greymass)"
  git clone --quiet --depth 1 https://github.com/greymass/buoy-client reference/buoy-client || { echo "FAIL  buoy-client"; failures=$((failures+1)); }
fi

echo "done ($failures failures)"
