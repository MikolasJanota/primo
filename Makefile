.PHONY: build test python-test asan-build asan-test smoke compare fuzz fuzz-finite-domain fuzzsmt tactic-composition-test bench bench-interesting-qf-uf docs docs-history docs-site docs-venv docs-serve archive lint tidy include-cleaner format check contrib contrib-required contrib-cli11 contrib-z3 contrib-cvc5 contrib-yices contrib-opensmt contrib-yinyang contrib-dolmen contrib-cadical contrib-gmp contrib-fuzzsmt contrib-ddsmt clean

BUILD_DIR ?= build
ASAN_BUILD_DIR ?= build-asan
PRIMO ?= $(BUILD_DIR)/primo
CONTRIB_PREFIX ?= contrib/local
CADICAL_TAG ?= rel-3.0.0
PYTHON ?= python3
RUFF ?= .venv/bin/python -m ruff
PATH_WITH_CONTRIB := $(CURDIR)/$(CONTRIB_PREFIX)/bin:$(PATH)
CPP_FORMAT_FILES := $$(rg --files src tests docs/examples -g '*.cpp' -g '*.hpp' -g '*.h')
STATIC_BUILD_DIR ?= build-static
STATIC_RUN_STAMP := $(shell date -u +%Y%m%dT%H%M%SZ)
STATIC_REPORT_DIR ?= results/static-$(STATIC_RUN_STAMP)
STATIC_JOBS ?= 2
STATIC_CXX ?= clang++-18
CLANG_TIDY ?= clang-tidy-18
RUN_CLANG_TIDY ?= /usr/bin/python3 /usr/bin/run-clang-tidy-18
CLI11_STAMP := $(CONTRIB_PREFIX)/.cli11.stamp
CADICAL_STAMP := $(CONTRIB_PREFIX)/.cadical.stamp
GMP_STAMP := $(CONTRIB_PREFIX)/.gmp.stamp

build: contrib-required
	cmake -S . -B $(BUILD_DIR) -DCMAKE_PREFIX_PATH=$(CURDIR)/$(CONTRIB_PREFIX)
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

python-test:
	$(PYTHON) -m unittest discover -s tests/python -p 'test_*.py'

asan-build: contrib-required
	cmake -S . -B $(ASAN_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$(CURDIR)/$(CONTRIB_PREFIX) -DPRIMO_SANITIZERS=address
	cmake --build $(ASAN_BUILD_DIR)

asan-test: asan-build
	ASAN_OPTIONS=detect_leaks=1:strict_init_order=1 ctest --test-dir $(ASAN_BUILD_DIR) --output-on-failure

smoke: build
	$(PYTHON) scripts/run_smtlib.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5

compare: build
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/compare_with_z3.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --refs z3 cvc5 --timeout 5 --out results/compare.csv

fuzz: build
	@if PATH="$(PATH_WITH_CONTRIB)" command -v typefuzz >/dev/null 2>&1; then \
		PATH="$(PATH_WITH_CONTRIB)" scripts/yinyang_fuzz.sh \
			--seeds benchmarks/seeds/qf_uf \
			--solver './$(PRIMO) --quiet' \
			--ref 'z3 model_validate=true' \
			--seconds 60 \
			--iterations 5 \
			--solver-timeout 5 \
			--bugs fuzz-results/qf_uf-yinyang/bugs \
			--scratch fuzz-results/qf_uf-yinyang/scratch; \
	else \
		PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/fuzz_external.py --fuzzer yinyang --seeds benchmarks/seeds/qf_uf --solver ./$(PRIMO) --refs z3 cvc5 --logic QF_UF --count 1 --timeout 5 --out fuzz-results/qf_uf-yinyang; \
	fi

fuzz-finite-domain: build
	@PATH="$(PATH_WITH_CONTRIB)" command -v z3 >/dev/null 2>&1 || { echo "z3 is required; run make contrib-z3" >&2; exit 2; }
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/finite_domain_fuzz.py --solver ./$(PRIMO) --solver-arg=--preprocessing-mode=strong --ref z3 --logic QF_UF --count 56 --timeout 5 --require-post-amo --out fuzz-results/finite-domain-qf-uf
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/finite_domain_fuzz.py --solver ./$(PRIMO) --ref z3 --logic QF_UFLRA --count 56 --timeout 5 --require-post-amo --out fuzz-results/finite-domain-qf-uflra

bench: build
	$(PYTHON) scripts/bench.py benchmarks/smoke/qf_uf/*.smt2 --solver ./$(PRIMO) --timeout 5 --out results/bench.csv

bench-interesting-qf-uf: build
	$(PYTHON) scripts/bench_interesting_qf_uf.py --solver ./$(PRIMO) --solver-arg=--quiet --timeout 30 --manifest benchmarks/interesting-qf-uf.txt --out results/interesting-qf-uf.csv

docs-venv:
	$(PYTHON) -m venv .venv-docs
	.venv-docs/bin/python -m pip --disable-pip-version-check install --upgrade pip
	.venv-docs/bin/python -m pip --disable-pip-version-check install -r docs/requirements.txt

docs:
	PRIMO_DOCS_DIR=docs bash scripts/build_docs.sh

docs-history:
	PRIMO_DOCS_DIR=docs-history bash scripts/build_docs.sh

docs-site: docs
	$(MAKE) docs-history
	$(PYTHON) scripts/assemble_docs.py

.PHONY: docs-full docs-check-links docs-check-external
docs-full:
	PRIMO_DOCS_DIR=docs bash scripts/build_docs.sh --all
	PRIMO_DOCS_DIR=docs-history bash scripts/build_docs.sh --all
	$(PYTHON) scripts/assemble_docs.py

docs-check-links: docs-full
	$(PYTHON) scripts/check_doc_links.py docs/_build/site --report docs/_build/linkcheck-local.json

docs-check-external: docs-check-links
	@status=0; for book in docs docs-history; do \
		PRIMO_DOCS_DIR=$$book bash scripts/build_docs.sh --all --builder custom --custom-builder linkcheck || status=1; \
	done; exit $$status

docs-serve: docs
	$(PYTHON) -m http.server 8000 -d docs/_build/html

archive: docs-site
	$(PYTHON) scripts/create_source_archive.py

lint:
	$(PYTHON) scripts/check_statistics_policy.py
	$(PYTHON) -m py_compile scripts/*.py contrib/*.py tests/python/*.py
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format --dry-run --Werror $(CPP_FORMAT_FILES); \
	else \
		echo "error: clang-format is not installed, and the C++ format check is not optional."; \
		echo "       It used to be skipped here, which let unformatted code reach main"; \
		echo "       unnoticed -- a lint gate that passes when the linter is absent is not"; \
		echo "       a gate. Install it (apt-get install clang-format) and re-run;"; \
		echo "       'make format' applies the repository .clang-format in place."; \
		exit 1; \
	fi
	@if $(RUFF) --version >/dev/null 2>&1; then \
		$(RUFF) check scripts contrib tests/python; \
	else \
		echo "error: Ruff is required for Python lint, including executable shebangs and import order."; \
		echo "       Install the CI-pinned version with .venv/bin/python -m pip install ruff==0.16.1"; \
		exit 1; \
	fi

.PHONY: analysis-db tidy include-cleaner check-headers
analysis-db:
	"$(CLANG_TIDY)" --version
	"$(STATIC_CXX)" --version
	cmake -S . -B "$(STATIC_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="$(STATIC_CXX)" -DCMAKE_PREFIX_PATH="$(CURDIR)/$(CONTRIB_PREFIX)"
	cmake -DPRIMO_SOURCE_DIR="$(CURDIR)" -DPRIMO_GIT_REVISION_HEADER="$(STATIC_BUILD_DIR)/generated/primo_git_revision.hpp" -P scripts/write_git_revision.cmake
	$(PYTHON) scripts/prepare_static_analysis.py --build "$(STATIC_BUILD_DIR)" --out "$(STATIC_REPORT_DIR)/database"

tidy: analysis-db
	@status=0; $(RUN_CLANG_TIDY) -p "$(STATIC_REPORT_DIR)/database" -j $(STATIC_JOBS) -clang-tidy-binary "$(CLANG_TIDY)" -export-fixes "$(STATIC_REPORT_DIR)/tidy.yaml" > "$(STATIC_REPORT_DIR)/tidy.log" 2>&1 || status=$$?; \
	$(PYTHON) scripts/summarize_static_analysis.py --manifest "$(STATIC_REPORT_DIR)/database/manifest.json" --log "$(STATIC_REPORT_DIR)/tidy.log" --exit-code $$status --reject-unused-includes --out "$(STATIC_REPORT_DIR)/tidy-summary.json"

include-cleaner: analysis-db
	@status=0; $(RUN_CLANG_TIDY) -p "$(STATIC_REPORT_DIR)/database" -j $(STATIC_JOBS) -clang-tidy-binary "$(CLANG_TIDY)" '-checks=-*,clang-diagnostic-*,misc-include-cleaner' -export-fixes "$(STATIC_REPORT_DIR)/includes.yaml" > "$(STATIC_REPORT_DIR)/includes.log" 2>&1 || status=$$?; \
	$(PYTHON) scripts/summarize_static_analysis.py --manifest "$(STATIC_REPORT_DIR)/database/manifest.json" --log "$(STATIC_REPORT_DIR)/includes.log" --exit-code $$status --reject-unused-includes --out "$(STATIC_REPORT_DIR)/includes-summary.json"

check-headers: analysis-db
	$(PYTHON) scripts/check_static_headers.py --database "$(STATIC_REPORT_DIR)/database/compile_commands.json" --out "$(STATIC_REPORT_DIR)/headers" --jobs $(STATIC_JOBS)

format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(CPP_FORMAT_FILES); \
	else \
		echo "clang-format is not installed; skipping C++ formatting."; \
	fi
	@if $(RUFF) --version >/dev/null 2>&1; then \
		$(RUFF) check --select I001 --fix scripts contrib tests/python; \
		$(RUFF) format scripts contrib tests/python; \
	else \
		echo "error: Ruff is required for Python formatting."; \
		exit 1; \
	fi

check: lint test python-test

fuzzsmt: build contrib-fuzzsmt contrib-z3
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/fuzz_external.py --fuzzer fuzzsmt --solver ./$(PRIMO) --refs z3 --logic QF_UF --count 20 --timeout 5 --fuzzer-args '-ms 1 -Ms 2 -mv 1 -Mv 2 -mf 1 -mp 1 -ma 1 -Ma 2' --out fuzz-results/qf_uf-fuzzsmt

tactic-composition-test: build
	PATH="$(PATH_WITH_CONTRIB)" $(PYTHON) scripts/tactic_composition_matrix.py --solver ./$(PRIMO) --refs z3 cvc5 --repeat 2 --out results/tactic-composition-local

contrib: contrib-required contrib-z3 contrib-cvc5 contrib-yices contrib-opensmt contrib-fuzzsmt contrib-ddsmt

contrib-required: contrib-cli11 contrib-cadical contrib-gmp

contrib-cli11: $(CLI11_STAMP)

$(CLI11_STAMP): contrib/install_cli11.py
	$(PYTHON) contrib/install_cli11.py --prefix $(CONTRIB_PREFIX)
	touch $(CLI11_STAMP)

contrib-z3:
	$(PYTHON) contrib/install_z3.py --prefix $(CONTRIB_PREFIX)

contrib-cvc5:
	$(PYTHON) contrib/install_cvc5.py --prefix $(CONTRIB_PREFIX)

# Reference solver only -- never linked, never read. See the header of
# contrib/install_yices.py for why that boundary is stated explicitly here.
contrib-yices:
	$(PYTHON) contrib/install_yices.py --prefix $(CONTRIB_PREFIX)

# Reference solver only -- same boundary as yices, and for the same reason:
# OpenSMT's QF_LRA solver descends from the same paper Primo's does.
contrib-opensmt:
	$(PYTHON) contrib/install_opensmt.py --prefix $(CONTRIB_PREFIX)

# Installs the fork with contrib/patches applied, into .venv-yinyang. Not part
# of `contrib`: it builds a venv rather than dropping a binary in
# contrib/local, and the fuzz targets skip cleanly when typefuzz is absent.
contrib-yinyang:
	$(PYTHON) contrib/install_yinyang.py

# Optional review tools, preserved by the remote preparation helper.
.PHONY: contrib-clang-tools
contrib-clang-tools:
	$(PYTHON) -m venv "$(CONTRIB_PREFIX)/clang-tools-env"
	"$(CONTRIB_PREFIX)/clang-tools-env/bin/python" -m pip install clang-tidy==18.1.8 clang-format==18.1.8
	"$(CONTRIB_PREFIX)/clang-tools-env/bin/clang-tidy" --version
	$(PYTHON) contrib/stage_clang_tidy_headers.py --cxx "$(CXX)" --out "$(CONTRIB_PREFIX)/clang-tools-env/cxx-headers"

# Model validator only. The installer uses a repository-local opam root and
# switch because the official Dolmen 0.10 Linux binary requires newer glibc
# than the SLURM environment.
contrib-dolmen:
	$(PYTHON) contrib/install_dolmen.py --prefix $(CONTRIB_PREFIX)

contrib-cadical: $(CADICAL_STAMP)

$(CADICAL_STAMP): contrib/install_cadical.py
	$(PYTHON) contrib/install_cadical.py --prefix $(CONTRIB_PREFIX) --tag $(CADICAL_TAG) --clean-build
	touch $(CADICAL_STAMP)

contrib-gmp: $(GMP_STAMP)

$(GMP_STAMP): contrib/install_gmp.py
	$(PYTHON) contrib/install_gmp.py --prefix $(CONTRIB_PREFIX)
	touch $(GMP_STAMP)

contrib-fuzzsmt:
	$(PYTHON) contrib/install_fuzzsmt.py --prefix $(CONTRIB_PREFIX)

contrib-ddsmt:
	$(PYTHON) contrib/install_ddsmt.py --prefix $(CONTRIB_PREFIX)

clean:
	rm -rf $(BUILD_DIR) results fuzz-results _build docs/_build docs-history/_build
