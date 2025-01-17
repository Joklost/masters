#include <ostream>
#include <iostream>
#include <csignal>
#include <thread>
#include <random>

#include <mpilib/hardware.h>
#include <mpilib/interface.h>

struct Packet {
    unsigned long rank{};
    octet key{};

    friend std::ostream &operator<<(std::ostream &os, const Packet &packet) {
        os << "Packet{rank: " << packet.rank << ", key: " << packet.key << "}";
        return os;
    }
};

template<class...Durations, class DurationIn>
std::tuple<Durations...> break_down_durations(DurationIn d) {
    std::tuple<Durations...> retval;
    using discard=int[];
    (void) discard{0, (void((
                                    (std::get<Durations>(retval) = std::chrono::duration_cast<Durations>(d)),
                                            (d -= std::chrono::duration_cast<DurationIn>(std::get<Durations>(retval)))
                            )), 0)...};
    return retval;
}

std::string format_duration(std::chrono::microseconds us) {
    auto dur = break_down_durations<std::chrono::seconds, std::chrono::milliseconds, std::chrono::microseconds>(us);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::get<0>(dur).count()
        << "::"
        << std::setw(3)
        << std::get<1>(dur).count()
        << "::"
        << std::setw(3)
        << std::get<2>(dur).count();
    return oss.str();
}

int main(int argc, char *argv[]) {
    if (argc > 1 && std::string{"--dummy"} == std::string{argv[1]}) {
        return 0;
    }

    auto debug = argc > 1 && std::string{"--debug"} == std::string{argv[1]};

    hardware::init(debug);
    auto id = hardware::get_id();
    auto rank = hardware::get_world_rank();
    auto &console = hardware::logger;
    auto slots = 5ul;
    std::random_device rd{};
    std::mt19937 eng{rd()};

    std::uniform_int_distribution<unsigned long> dist{0ul, slots};
    Packet secret{};
    if (rank == 2ul) {
        hardware::sleep(10ms);
        secret = Packet{id, 0x42};
        hardware::broadcast(mpilib::serialise(secret));
    } else {
        auto data = hardware::listen(200ms);

        if (!data.empty()) {
            auto localtime = hardware::get_localtime();
            secret = mpilib::deserialise<Packet>(data);
            console->info("{} - {}", format_duration(localtime), secret);
        }
    }

//    auto slot_length = 2s;
//    for (auto i = 0; i < 10; ++i) {
//        auto selected = dist(eng);
//        for (auto current = 0; current < slots; ++current) {
//            if (selected == current) {
//                if (secret.rank != 0ul) {
//                    hardware::noop(20ms);
//                    auto duration = hardware::transmit(mpilib::serialise(secret));
//                    hardware::noop(slot_length - duration - 20ms);
//                    continue;
//                }
//
//                hardware::noop(slot_length);
//            } else {
//                if (secret.rank != 0ul) {
//                    hardware::noop(slot_length);
//                    continue;
//                }
//
//                auto localtime = hardware::get_localtime();
//
//                auto packet = hardware::receive(slot_length);
//                if (!packet.empty()) {
//                    secret = mpilib::deserialise<Packet>(packet);
//                }
//
//                auto duration = hardware::get_localtime() - localtime;
//                hardware::noop(slot_length - duration);
//            }
//        }
//    }


//    auto localtime = hardware::get_localtime();
//    if (secret.rank != 0ul) {
//        console->info("{} - {}", format_duration(localtime), secret);
//    } else {
//        console->info("{} - Nothing received!", format_duration(localtime));
//    }

    hardware::deinit();

    return 0;
}