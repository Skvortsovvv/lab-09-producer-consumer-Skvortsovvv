// Copyright 2020 Your Name <your_email>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_

#include <boost/program_options.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <gumbo.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <mutex>
#include <thread>
#include <ThreadPool.h>
#include <chrono>
#include <iomanip>

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace po = boost::program_options;

class Queue {
public:
    void push(std::string& str) {
        std::scoped_lock<std::mutex> lock{ mut };
        queue_.push(str);
    }
    std::string front() {
        std::scoped_lock<std::mutex> lock{ mut };
        return queue_.front();
    }
    void pop() {
        std::scoped_lock<std::mutex> lock{ mut };
        queue_.pop();
    }
    bool empty() {
        std::scoped_lock<std::mutex> lock{ mut };
        return queue_.empty();
    }

private:
    std::mutex mut;
    std::queue<std::string> queue_;
};

Queue picture_url_queue;
Queue html_queue;
Queue url_queue;
std::vector<std::string> url_to_file;

class Parser {
private:
    ThreadPool consumers;
    ThreadPool thread_out{ 1 };
    std::string path;
    std::ofstream ofs;
public:
    Parser(int consume, std::string path_):consumers(consume), path(path_){

    }
    void parsing() {
        while (true) {
            if (html_queue.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (html_queue.empty()) {
                    break;
                }
                else {
                    continue;
                }
            }
            consumers.enqueue(&Parser::search_links, this, html_queue.front());
            html_queue.pop();
        }
        write_out();
    }

    void write_out() {
        while (true) {
            if (picture_url_queue.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (picture_url_queue.empty()) {
                    break;
                }
                else {
                    continue;
                }
            }
            std::string res = picture_url_queue.front();
            url_to_file.push_back(res);
            picture_url_queue.pop();
        }
        ofs.open(path);
        if (ofs.is_open()) {
            for (auto& i : url_to_file) {
                std::cout << i << std::endl;
                ofs << i << std::endl;
            }
            ofs.close();
        }
        else {
            std::cerr << "FILE CANT BE OPENED" << std::endl;
        }
    }


    static void for_search_links(GumboNode* node, std::vector<std::string>& pict, std::vector<std::string>& url) {
        if (node->type != GUMBO_NODE_ELEMENT) {
            return;
        }
        GumboAttribute* href;
        if (node->v.element.tag == GUMBO_TAG_A &&
            (href = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
            url.push_back(href->value);
        }
        GumboAttribute* src;
        if (node->v.element.tag == GUMBO_TAG_A &&
            (src = gumbo_get_attribute(&node->v.element.attributes, "src"))) {
            pict.push_back(src->value);
        }
        GumboVector* children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            for_search_links(static_cast<GumboNode*>(children->data[i]), pict, url);
        }
    }

    void search_links(std::string& html) {
        std::vector<std::string> pict_url;
        std::vector<std::string> urls;
        std::stringstream sstr(html);
        std::stringstream in(html, std::ios::in | std::ios::binary);
        if (!in) {
            exit(EXIT_FAILURE);
        }
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        GumboOutput* output = gumbo_parse(contents.c_str());
        for_search_links(output->root, pict_url, urls);
        gumbo_destroy_output(&kGumboDefaultOptions, output);

        for (auto& i : sub_URL(pict_url)) {
            picture_url_queue.push(i);
        }
        for (auto& i : sub_URL(urls)) {
            url_queue.push(i);
        }
        return;
    }


    std::vector<std::string> sub_URL(std::vector<std::string> URLs) {
        std::string URL;
        bool record = false;
        std::vector<std::string> vector_url;
        for (int i = 0; i < URLs.size(); ++i) {
            if (URLs[i][0] == 'h' != record) {
                URL += URLs[i];
                record = true;
            }
            else if (URLs[i][0] == 'h') {
                std::cout << URL << std::endl;
                vector_url.push_back(URL);
                URL = "";
                URL += URLs[i];
            }
        }
        return vector_url;
    }


};


class Downloader {
private:
    ThreadPool producers;
    int depth;
public:

    Downloader(int prod, int depth_):producers(prod), depth(depth_){}

    void proccesing(const std::string& host, const std::string& target, Parser& parser) {
        std::string first_html = download_HTML(host, target);
        parser.parsing();
        while (depth > 0) {
            next_step();
            --depth;
        }
    }

    void next_step() {
        while (true) {
            if (html_queue.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (html_queue.empty()) {
                    break;
                }
                else {
                    continue;
                }
            }
            auto url_parts = parsing_URL(html_queue.front());
            producers.enqueue(&Downloader::download_HTML, this, url_parts[0], url_parts[1]);
            html_queue.pop();
        }
    }

    std::string download_HTML(const std::string& host, const std::string& target) {

        try {
            boost::asio::io_context ioc;
            tcp::resolver resolver{ ioc };
            tcp::socket socket{ ioc };
            auto const results = resolver.resolve(host, "443");
            boost::asio::connect(socket, results.begin(), results.end());
            http::request<http::string_body> req{ http::verb::get, target, 11 };
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            http::write(socket, req);
            boost::beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(socket, buffer, res);

            std::string result;
            result = boost::beast::buffers_to_string(res.body().data());
            //std::cout << result << std::endl;

            boost::system::error_code ec;
            socket.shutdown(tcp::socket::shutdown_both, ec);
            if (ec && ec != boost::system::errc::not_connected)
                throw boost::system::system_error{ ec };
            html_queue.push(result);
            return result;
        }
        catch (std::exception const& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            return "";

        }
        return "";
    }

    std::vector<std::string> parsing_URL(const std::string& URL) {
        std::vector<std::string> UrlParts;
        std::string right_part = URL.substr(URL.find("//") + 2);
        return { right_part.substr(0, right_part.find('/')) ,
            right_part.substr(right_part.find('/')) };
    }
};



std::vector<std::string>& operator+=(std::vector<std::string>& v1, std::vector<std::string>& v2) {
    for (int i = 0; i < v2.size(); ++i) {
        v1.push_back(v2[i]);
    }
    return v1;
}

// ---------------------------- Parsing CMD arguments -------------------------

std::vector<std::string> params(po::variables_map& vm) {
    std::string url_, output_;
    int depth_, net_threads_, parser_threads_;
    std::vector<std::string> parametrs;
    if (vm.count("url")) {
        url_ = vm["url"].as<std::string>();
        parametrs.push_back(url_);
    }
    if (vm.count("depth")) {
        depth_ = vm["depth"].as<int>();
        parametrs.push_back(std::to_string(depth_));

    }
    if (vm.count("network_threads")) {
        net_threads_ = vm["network_threads"].as<int>();
        parametrs.push_back(std::to_string(net_threads_));
    }
    if (vm.count("parser_threads")) {
        parser_threads_ = vm["parser_threads"].as<int>();
        parametrs.push_back(std::to_string(parser_threads_));
    }
    if (vm.count("output")) {
        output_ = vm["output"].as<std::string>();
        parametrs.push_back(output_);
    }
    return parametrs;
}


std::vector<std::string> ParsingCMD(int argc, char** argv) {
    po::options_description desc("General options");
    desc.add_options()
        ("url,u", po::value<std::string>(), "Site url")
        ("depth,d", po::value<int>(), "Depth of parse")
        ("network_threads,n", po::value<int>(), "Amount of threads producers")
        ("parser_threads,p", po::value<int>(), "Amount of threads consumers")
        ("output,o", po::value<std::string>(), "Output file .txt");
    po::variables_map vm;
    po::parsed_options parsed = po::command_line_parser(argc, argv)
        .options(desc).allow_unregistered().run();
    po::store(parsed, vm);
    po::notify(vm);
    po::store(po::parse_command_line(argc, argv, desc), vm);
    return params(vm);
}



#endif // INCLUDE_HEADER_HPP_
