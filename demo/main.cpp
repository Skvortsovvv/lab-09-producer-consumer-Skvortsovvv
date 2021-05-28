#include <header.hpp>

int main(int argc, char** argv)
{
    if (argc != 6) {
        std::cerr << "Usage: --url=<link> --depth=<number> --network_threads=<number> --parser_threads=<number> --output=*.txt \n" <<
            "Example: --url=yandex.com --depth=1 --network_threads=3 --parser_threads=2 --output=output.txt \n";
        return EXIT_FAILURE;
    }
    std::vector<std::string> Arguments = ParsingCMD(argc, argv);
    Downloader DL(std::stoi(Arguments[2]), std::stoi(Arguments[1]));
    Parser pars(std::stoi(Arguments[3]), Arguments[4]);
    auto url = DL.parsing_URL(Arguments[0]);
    DL.proccesing(url[0], url[1], pars);
 
}
