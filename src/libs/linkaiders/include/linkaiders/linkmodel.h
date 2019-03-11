#ifndef LINKAIDERS_LINKMODEL_H
#define LINKAIDERS_LINKMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the link model.
 *
 * Will return nullptr if initialization fails.
 *
 * @param num_nodes Amount of nodes in the network
 * @param nchans Amount of channels in the network
 * @param gpslog Filepath for a log of GPS coordinates for all nodes
 * @return The link model object
 */
void *initialize(int num_nodes, int nchans, const char *gpslog);

/**
 * Deinitialize the link model.
 * @param model The link model object
 */
void deinit(void *model);

/**
 * Checks whether two nodes are connected at a given time (based on distance).
 * @param model The link model object
 * @param x Node identifier
 * @param y Node identifier
 * @param timestamp Timestamp to check
 * @return True if the nodes are connected
 */
bool is_connected(void *model, int x, int y, unsigned long timestamp);

/**
 * Notify the link model that a node starts sending on a specific channel.
 * @param model The link model object
 * @param id Node identifier
 * @param chn Channel identifier
 * @param timestamp Timestamp to start sending
 */
void begin_send(void *model, int id, int chn, unsigned long timestamp);

/**
 * Notify the link model that a node stops sending on a specific channel.
 * @param model The link model object
 * @param id Node identifier
 * @param chn Channel identifier
 * @param timestamp Timestamp to stop sending
 */
void end_send(void *model, int id, int chn, unsigned long timestamp);

/**
 * Notify the link model that a node starts listening on a specific channel.
 * @param model The link model object
 * @param id Node identifier
 * @param chn Channel identifier
 * @param timestamp Timestamp to start listening
 */
void begin_listen(void *model, int id, int chn, unsigned long timestamp);

/**
 * Returns node identifier of the other node if transmission was completed before timestamp.
 * @param model The link model object
 * @param id Node identifier
 * @param chn Channel identifier
 * @param timestamp Timestamp to check for completion
 * @return Node identifier of receiving node.
 */
int status(void *model, int id, int chn, unsigned long timestamp);

/**
 * Notify the link model that a node stops listening on a specific channel.
 * @param model The link model object
 * @param id Node identifier
 * @param chn Channel identifier
 * @param timestamp Timestamp to stop listening
 * @return Node identifier of receiving node.
 */
int end_listen(void *model, int id, int chn, unsigned long timestamp);

#ifdef __cplusplus
}
#endif

#endif /* LINKAIDERS_LINKMODEL_H */