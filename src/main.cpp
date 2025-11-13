#include "microphone.hpp"
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/module/service.hpp>

#include <iostream>
#include <memory>

namespace vsdk = ::viam::sdk;


std::vector<std::shared_ptr<vsdk::ModelRegistration>> create_all_model_registrations() {
    std::vector<std::shared_ptr<vsdk::ModelRegistration>> registrations;

    registrations.push_back(std::make_shared<vsdk::ModelRegistration>(
        vsdk::API::get<vsdk::AudioIn>(),
        microphone::Microphone::model,
        [](vsdk::Dependencies deps, vsdk::ResourceConfig config) {
            return std::make_unique<microphone::Microphone>(std::move(deps), std::move(config));
        },
        microphone::Microphone::validate));

    return registrations;
}


int serve(int argc, char** argv) try {
    // Every Viam C++ SDK program must have one and only one Instance object
    // which is created before any other C++ SDK objects and stays alive until
    // all Viam C++ SDK objects are destroyed.
    vsdk::Instance inst;

    microphone::startPortAudio();
    auto module_service = std::make_shared<vsdk::ModuleService>(argc, argv, create_all_model_registrations());
    module_service->serve();

    return EXIT_SUCCESS;
} catch (const std::exception& ex) {
    std::cerr << "ERROR: A std::exception was thrown from `serve`: " << ex.what() << std::endl;
    return EXIT_FAILURE;
} catch (...) {
    std::cerr << "ERROR: An unknown exception was thrown from `serve`" << std::endl;
    return EXIT_FAILURE;
}

int main(int argc, char* argv[]) {
    const std::string usage = "usage: audio /path/to/unix/socket";
    if (argc < 2) {
        std::cout << "ERROR: insufficient arguments\n";
        std::cout << usage << "\n";
        return EXIT_FAILURE;
    }

    return serve(argc, argv);
};



