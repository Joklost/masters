#include <random>

#include <mpilib/geomath.h>
#include <reachi/radiomodel.h>

#include "coordr.h"

#define TX_POWER 26.0
#define THRESHOLD -110.0 /* RSSI in dBm */


bool Coordinator::has_link(Coordinator::Listen &rx, Coordinator::Transmission &tx) {
    /* First condition should not be possible? */
    if (rx.rank == tx.rank || !tx.is_within(rx)) {
        return false;
    }

    auto pathloss = this->link_model[rx.rank][tx.rank];
    auto rssi = TX_POWER - pathloss;

    /* If the distance is greater than the threshold
     * we know that the packet will be dropped. */
    return !(rssi > THRESHOLD || mpilib::is_zero(rssi));

}


void Coordinator::run() {
    std::random_device rd;
    std::mt19937 gen(rd());

    mpi::init(&this->world_size, &this->world_rank, &this->name_len, this->processor_name);
    this->world_size = this->world_size - 2;
    this->lmc_node = this->world_size + 1;

    this->c = spdlog::stdout_color_mt("coordr");
    if (this->debug) {
        this->c->set_level(spdlog::level::debug);
    }

    this->c->debug("init(nodes={})", this->world_size);

    if (!this->handshake()) {
        this->c->debug("deinit()");
        mpi::send(DIE, this->lmc_node, DIE);
        mpi::deinit();
        return;
    }

    while (true) {

        auto status = mpi::probe();

        if (status.tag == DIE) {
            auto localtime = mpi::recv<unsigned long>(status.source, DIE);
            this->c->debug("die(source={}, tag={}, localtime={})", status.source, status.tag, localtime);

            this->nodes[status.source].localtime = std::numeric_limits<unsigned long>::max() - 1ul;
            this->nodes[status.source].dead = true;

            auto all_dead = true;
            for (const auto &node : this->nodes) {
                if (!node.second.dead) {
                    all_dead = false;
                    break;
                }
            }

            if (all_dead) {
                /* Break the loop to deinit mpi. */
                break;
            }

        } else if (status.tag == TX_PKT) {
            this->transmit_actions.emplace_back();
            auto &tx = this->transmit_actions.back();
            tx.rank = status.source;
            tx.start = mpi::recv<unsigned long>(status.source, TX_PKT);
            tx.data = mpi::recv<std::vector<octet>>(status.source, TX_PKT_DATA);
            tx.end = tx.start + mpilib::compute_transmission_time(BAUDRATE, tx.data.size()).count();
            this->nodes[status.source].localtime = tx.end;

            for (auto &rx : this->listen_actions) {
                if (this->has_link(rx, tx)) {
                    rx.transmissions.push_back(tx);
                }
            }

        } else if (status.tag == RX_PKT) {
            this->listen_actions.emplace_back();
            auto &rx = this->listen_actions.back();
            rx.rank = status.source;
            rx.start = mpi::recv<unsigned long>(status.source, RX_PKT);
            rx.end = rx.start + mpi::recv<unsigned long>(status.source, RX_PKT_DURATION);
            this->nodes[status.source].localtime = rx.end;

            for (auto &tx : this->transmit_actions) {
                if (this->has_link(rx, tx)) {
                    rx.transmissions.push_back(tx);
                }
            }

        } else if (status.tag == SLEEP) {
            auto &node = this->nodes[status.source];
            auto localtime = mpi::recv<unsigned long>(status.source, SLEEP);
            auto duration = mpi::recv<unsigned long>(status.source, SLEEP_DURATION);
            node.localtime = localtime + duration;

        } else if (status.tag == SET_LOCATION) {
            auto &node = this->nodes[status.source];
            node.loc = update_location(status.source);

        } else if (status.tag == SET_LOCAL_TIME) {
            auto &node = this->nodes[status.source];
            node.localtime = mpi::recv<unsigned long>(status.source, SET_LOCAL_TIME);

        } else if (status.tag == LINK_MODEL) {
            auto count = mpi::recv<unsigned long>(status.source, LINK_MODEL);
            std::vector<mpilib::Link> link_model{};
            link_model.reserve(count);
            for (auto i = 0; i < count; ++i) {
                auto link_buffer = mpi::recv<std::vector<octet>>(status.source, LINK_MODEL_LINK);
                link_model.push_back(mpilib::deserialise<mpilib::Link>(link_buffer));
            }

        }

        /* Process Actions */
        if (this->listen_actions.empty()) {
            continue;
        }

        unsigned long min_time = std::numeric_limits<unsigned long>::max();
        for (const auto &item : this->nodes) {
            auto &node = item.second;
            if (node.localtime < min_time) {
                min_time = node.localtime;
            }
        }

        if (min_time == std::numeric_limits<unsigned long>::max()) {
            continue;
        }

        for (auto &rx : this->listen_actions) {
            if (rx.end > min_time) {
                continue;
            }

            std::vector<std::vector<octet>> packets{};

            //rx.processed = true;

            /* We have already checked if a link exists. Compute probability for packet error. */
            for (auto &tx : rx.transmissions) {
                auto rssi = TX_POWER - this->link_model[tx.rank][rx.rank];
                std::vector<double> interference{};

                /* Compute interfering transmitters. */
                for (auto &interfering_tx : this->transmit_actions) {
                    if (tx.rank == interfering_tx.rank) {
                        continue;
                    }

                    /* Check if transmissions intersect. */
                    if (tx.end <= interfering_tx.start || tx.start >= interfering_tx.end) {
                        continue;
                    }

                    auto interfering_pathloss = this->link_model[tx.rank][interfering_tx.rank];
                    auto rssi_i = TX_POWER - interfering_pathloss;
                    if (rssi_i > THRESHOLD || mpilib::is_zero(interfering_pathloss)) {
                        continue;
                    }

                    interference.push_back(rssi_i);
                }

                auto pep = reachi::radiomodel::pep(rssi, tx.data.size(), interference);
                std::bernoulli_distribution d(1.0 - pep);
                auto should_receive = d(gen);

                if (should_receive) {
                    packets.emplace_back(tx.data);
                }
            }

            mpi::send(packets.size(), rx.rank, RX_PKT_COUNT);

            for (auto &packet : packets) {
                mpi::send(packet, rx.rank, RX_PKT_DATA);
            }
        }
    }

    this->c->debug("deinit()");
    mpi::send(DIE, this->lmc_node, DIE);
    mpi::deinit();
}


bool Coordinator::handshake() {
    mpi::send(this->lmc_node, this->lmc_node, HANDSHAKE);

    for (auto i = 1; i <= world_size; ++i) {
        auto node_name = mpilib::processor_name(this->processor_name, i);
        mpi::send(i, i, HANDSHAKE);
        auto response = mpi::recv<int>(i, HANDSHAKE);
        if (response == i) {
            this->nodes.insert({i, {i}});
            this->c->debug("handshake(target={}, status=success)", node_name);

            mpi::Status status = mpi::probe(i, SET_LOCATION);
            update_location(status.source);
            auto node_buffer = mpilib::serialise(this->nodes[i]);
            mpi::send(node_buffer, this->lmc_node, NODE_INFO);

        } else {
            this->c->debug("handshake(target={}, status=fail)", node_name);
            return false;
        }
    }

    auto response = mpi::recv<int>(this->lmc_node, HANDSHAKE);

    if (response != this->lmc_node) {
        this->c->debug("handshake(target=lmc, status=fail)");
        return false;
    } else {
        this->c->debug("handshake(target=lmc, status=success)");
    }

    auto count = mpi::recv<unsigned long>(this->lmc_node, LINK_MODEL);
    std::vector<mpilib::Link> link_model{};
    link_model.reserve(count);
    for (auto j = 0; j < count; ++j) {
        auto link_buffer = mpi::recv<std::vector<octet>>(this->lmc_node, LINK_MODEL_LINK);
        link_model.push_back(mpilib::deserialise<mpilib::Link>(link_buffer));
    }

    auto links = link_model.size();
    this->c->debug("set_linkmodel(links={})", link_model.size());

    std::vector<std::vector<double>> new_link_model{};
    new_link_model.resize(links, std::vector<double>(links));

    for (auto &item : link_model) {
        new_link_model[item.first][item.second] = item.pathloss;
        new_link_model[item.second][item.first] = item.pathloss;
    }
    this->link_model = std::move(new_link_model);

    /* Set the clocks on all nodes. */
    for (auto i = 1; i <= world_size; ++i) {
        mpi::send(i, i, READY);
    }

    return true;
}


mpilib::geo::Location Coordinator::update_location(const int rank) {
    auto buffer = mpi::recv<std::vector<octet>>(rank, SET_LOCATION);
    auto loc = mpilib::deserialise<mpilib::geo::Location>(buffer);

    this->c->debug("update_location(source={}, loc={})", rank, loc);
    this->nodes[rank].loc = loc;
    return loc;
}


bool Coordinator::Transmission::is_within(Coordinator::Listen &listen) {
    return this->start >= listen.start &&
           this->end <= listen.end;
}
