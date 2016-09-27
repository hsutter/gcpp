#!/usr/bin/env python
import sys
import os
import subprocess

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", help="run tests", action="store_true", dest="run_tests")
    parser.add_argument("--output", help="output dir (relative to source dir)", default="build", dest="out_dir")
    parser.add_argument("--config", help="build config", default="Debug", dest="config")
    args = parser.parse_args()
    
    src_dir = os.path.dirname(os.path.dirname(__file__))
    
    subprocess.check_call("cmake . -B{} -DCMAKE_BUILD_TYPE={}".format(args.out_dir, args.config).split(), cwd=src_dir)
    subprocess.check_call("cmake --build ./{}".format(args.out_dir).split(), cwd=src_dir)
    
    if args.run_tests:
        rc = subprocess.call("ctest . --output-on-failure".split(), cwd=os.path.join(src_dir,args.out_dir))
        if rc != 0:
            sys.exit(1)

if __name__ == "__main__":
    main()

