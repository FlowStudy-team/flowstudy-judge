#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "(not found)";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    int box_id = 0;
    std::string box = "/var/local/lib/isolate/" + std::to_string(box_id) + "/box/";
    std::string isolate_bin = "/usr/local/bin/isolate";

    // Init
    system((isolate_bin + " --cleanup --box-id=" + std::to_string(box_id)).c_str());
    int ret = system((isolate_bin + " --init --box-id=" + std::to_string(box_id)).c_str());
    std::cout << "init: " << ret << std::endl;
    chmod(box.c_str(), 0777);

    // Write source
    std::ofstream src(box + "solution.cpp");
    src << "#include <iostream>\nint main() {\n    int a, b;\n    std::cin >> a >> b;\n    std::cout << a + b << std::endl;\n    return 0;\n}";
    src.close();
    std::cout << "source written" << std::endl;

    // Compile
    std::ostringstream cmd;
    cmd << isolate_bin
        << " --box-id=" << box_id
        << " --env=PATH=/usr/bin:/bin"
        << " --processes=50 --wall-time=30"
        << " --meta=" << box << "cm.txt"
        << " --stderr=ce.txt"
        << " --run -- /usr/bin/g++ -std=c++17 -O2 -Wall -o solution solution.cpp";

    std::cout << "CMD: " << cmd.str() << std::endl;
    int cr = system(cmd.str().c_str());
    std::cout << "compile ret: " << cr << std::endl;
    std::cout << "compile meta: " << read_file(box + "cm.txt") << std::endl;
    std::cout << "compile stderr: " << read_file(box + "ce.txt") << std::endl;

    // Write input
    std::ofstream inp(box + "input.txt");
    inp << "1 2";
    inp.close();

    // Run
    std::ostringstream rcmd;
    rcmd << isolate_bin
         << " --box-id=" << box_id
         << " --env=PATH=/usr/bin:/bin"
         << " --processes=1 --wall-time=5"
         << " --meta=" << box << "rm.txt"
         << " --stdin=input.txt"
         << " --stdout=out.txt"
         << " --stderr=err.txt"
         << " --run -- ./solution";

    std::cout << "RCMD: " << rcmd.str() << std::endl;
    int rr = system(rcmd.str().c_str());
    std::cout << "run ret: " << rr << std::endl;
    std::cout << "run meta: " << read_file(box + "rm.txt") << std::endl;
    std::cout << "run stdout: " << read_file(box + "out.txt") << std::endl;
    std::cout << "run stderr: " << read_file(box + "err.txt") << std::endl;

    return 0;
}
