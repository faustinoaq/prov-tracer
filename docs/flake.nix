{
  inputs = {
    flake-utils = {
      url = "github:numtide/flake-utils";
    };
    nix-utils = {
      url = "github:charmoniumQ/nix-utils";
    };
    nix-documents = {
      url = "github:charmoniumQ/nix-documents";
    };
  };
  outputs = { self, nixpkgs, flake-utils, nix-utils, nix-documents }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          nix-lib = nixpkgs.lib;
          nix-utils-lib = nix-utils.lib.${system};
          nix-documents-lib = nix-documents.lib.${system};
          svg2pdf = dstName: srcName: pkgs.runCommand dstName {} "${pkgs.librsvg}/bin/rsvg-convert --output=$out --format=pdf ${srcName}";
          texlivePackages = {
            inherit (pkgs.texlive)
              # See sec 2.1 here https://ctan.math.illinois.edu/macros/latex/contrib/acmart/acmguide.pdf
              # Also see \usepackage's in acm-template.tex
              acmart
              amscls
              amsfonts
              amsmath
              babel
              bibcop
              biblatex
              biblatex-trad
              kastrup # \usepackage{binhex}
              bookmark
              booktabs
              caption
              catchfile
              cleveref
              cm-super
              cmap
              collection-luatex
              comment
              doclicense
              draftwatermark
              environ
              etoolbox
              fancyhdr
              fancyvrb
              float
              fontaxes
              fontspec
              geometry
              graphics
              hyperref
              hyperxmp
              ifmtarg
              iftex
              inconsolata
              lacheck
              latexmk
              libertine
              mdwtools
              microtype
              mmap
              ms
              mweights
              nag
              natbib
              ncctools # \usepackage{manyfoot,nccfoots}
              newtx
              oberdiek
              parskip
              pbalance
              physics
              # pdftex-def
              preprint
              printlen
              refcount
              scheme-small
              selnolig
              setspace
              subfig
              supertabular
              svg
              textcase
              tools
              totpages
              transparent
              trimspaces
              ulem
              upquote
              xcolor
              xkeyval
              xstring
              xurl
              zref
            ;
          };
        in
        {
          packages = {
            test = (pkgs.texlive.combine texlivePackages);
            default = nix-utils-lib.mergeDerivations {
              packageSet = nix-utils-lib.packageSetRec
                (self: [
                  (pkgs.stdenvNoCC.mkDerivation rec {
                    src = nix-utils-lib.mergeDerivations {
                      packageSet = {
                        "." = ./benchmark_suite;
                        "zotero.bib" = ./zotero.bib;
                        "reed.bib" = ./reed.bib;
                        "acm-template.tex" = ./acm-template.tex;
                        "citations-to-latex.lua" = ./citations-to-latex.lua;
                      }
                      // nix-utils-lib.packageSet [
                        self."app-lvl-prov.pdf"
                        self."wf-lvl-prov.pdf"
                        self."sys-lvl-prov.pdf"
                        self."sys-lvl-log.pdf"
                        self."prov-example.pdf"
                      ];
                    };
                    mainSrc = "README.md";
                    latexStem = "main";
                    latexTemplate = "acm-template.tex";
                    name = "benchmark_suite";
                    date = 1707292740;
                    latexmkFlagForEngine = "-pdf";
                    pandocFlagForEngine = "latexmk"; # pdfLaTeX vs LuaLaTeX vs XeLaTeX
                    FONTCONFIG_FILE = pkgs.makeFontsConf { fontDirectories = [ ]; };
                    buildInputs = [
                      (pkgs.texlive.combine texlivePackages)
                      pkgs.pandoc
                    ];
                    buildPhase = ''
                      tmp=$(mktemp --directory)
                      HOME=$(mktemp --directory)
                      export SOURCE_DATE_EPOCH=${builtins.toString date}
                      ${pkgs.pandoc}/bin/pandoc \
                           --output=${latexStem}.tex \
                           --lua-filter=citations-to-latex.lua \
                           --pdf-engine=${pandocFlagForEngine} \
                           --template=${latexTemplate} \
                           ${mainSrc}
                      # ${pkgs.pandoc}/bin/pandoc --output=${latexStem}.docx ${mainSrc}
                      # lacheck ${latexStem}.tex
                      set +e
                      latexmk ${latexmkFlagForEngine} -shell-escape -emulate-aux-dir -auxdir=$tmp -Werror ${latexStem}
                      latexmk_status=$?
                      set -e
                      mkdir $out/
                      cp *.{svg,pdf,bbl,tex,docx} $out/
                      if [ $latexmk_status -ne 0 ]; then
                        mv $tmp/${latexStem}.log $out
                        echo "Aborting: Latexmk failed"
                        # exit $latexmk_status
                      fi
                    '';
                    phases = [ "unpackPhase" "buildPhase" ];
                  })
                  (pkgs.stdenvNoCC.mkDerivation rec {
                    src = nix-utils-lib.mergeDerivations {
                      packageSet = {
                        "test.tex" = ./test.tex;
                      };
                    };
                    latexStem = "test";
                    latexTemplate = "acm-template.tex";
                    name = "test";
                    date = 1707292740;
                    latexmkFlagForEngine = "-pdf";
                    pandocFlagForEngine = "latexmk"; # pdfLaTeX vs LuaLaTeX vs XeLaTeX
                    FONTCONFIG_FILE = pkgs.makeFontsConf { fontDirectories = [ ]; };
                    buildInputs = [
                      (pkgs.texlive.combine texlivePackages)
                    ];
                    buildPhase = ''
                      tmp=$(mktemp --directory)
                      HOME=$(mktemp --directory)
                      export SOURCE_DATE_EPOCH=${builtins.toString date}
                      set +e
                      latexmk ${latexmkFlagForEngine} -shell-escape -emulate-aux-dir -auxdir=$tmp -Werror ${latexStem}
                      latexmk_status=$?
                      set -e
                      mkdir $out/
                      ls -ahlt
                      cp *.{svg,pdf,bbl,tex,docx} $out/
                      ls -ahlt $out/
                      if [ $latexmk_status -ne 0 ]; then
                        mv $tmp/${latexStem}.log $out
                        cat $out/${latexStem}.log
                        echo "Aborting: Latexmk failed"
                        # exit $latexmk_status
                      fi
                    '';
                    phases = [ "unpackPhase" "buildPhase" ];
                  })
                  (nix-documents-lib.graphvizFigure {
                    src = ./benchmark_suite;
                    main = "app-lvl-prov.dot";
                    name = "app-lvl-prov.pdf";
                    outputFormat = "pdf";
                    layoutEngine = "dot";
                  })
                  (nix-documents-lib.graphvizFigure {
                    src = ./benchmark_suite;
                    main = "wf-lvl-prov.dot";
                    name = "wf-lvl-prov.pdf";
                    outputFormat = "pdf";
                    layoutEngine = "dot";
                  })
                  (nix-documents-lib.graphvizFigure {
                    src = ./benchmark_suite;
                    main = "sys-lvl-prov.dot";
                    name = "sys-lvl-prov.pdf";
                    outputFormat = "pdf";
                    layoutEngine = "dot";
                  })
                  (nix-documents-lib.graphvizFigure {
                    src = ./benchmark_suite;
                    main = "prov-example.dot";
                    name = "prov-example.pdf";
                    outputFormat = "pdf";
                    layoutEngine = "dot";
                  })
                  (svg2pdf "sys-lvl-log.pdf" ./benchmark_suite/sys-lvl-log.svg)
                ]);
            };
          };
        });
}
