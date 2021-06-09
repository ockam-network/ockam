defmodule Ockam.Example.Stream.BiDirectional.Local do
  @moduledoc """

  Ping-pong example for bi-directional stream communication using local subsctiption

  Use-case: integrate ockam nodes which implement stream protocol consumer and publisher

  Pre-requisites:

  Ockam hub running with stream service and TCP listener

  Two ockam nodes "ping" and "pong"

  Expected behaviour:

  Two nodes "ping" and "pong" send messages to each other using two streams:
  "pong_topic" to send messages to "pong" node
  "ping_topic" to send messages to "ping" node

  Implementation:

  Stream service is running on the hub node

  Ping and pong nodes create local consumers and publishers to exchange messages
  """
  alias Ockam.Example.Stream.Ping
  alias Ockam.Example.Stream.Pong

  alias Ockam.Stream.Client.BiDirectional
  alias Ockam.Stream.Client.BiDirectional.PublisherRegistry

  def config() do
    %{
      hub_ip: "127.0.0.1",
      hub_port: 4000,
      service_address: "stream_service",
      index_address: "stream_index_service"
    }
  end

  def stream_options() do
    config = config()

    {:ok, hub_ip_n} = :inet.parse_address(to_charlist(config.hub_ip))
    tcp_address = %Ockam.Transport.TCPAddress{ip: hub_ip_n, port: config.hub_port}

    [
      service_route: [tcp_address, config.service_address],
      index_route: [tcp_address, config.index_address],
      partitions: 1
    ]
  end

  ## This should be run on the PONG node
  def init_pong() do
    ensure_tcp(5000)
    ## PONG worker
    {:ok, "pong"} = Pong.create(address: "pong")

    ## Create a local subscription to forward pong_topic messages to local node
    subscribe("pong_topic", "pong")
  end

  def run() do
    ensure_tcp(3000)

    ## PING worker
    Ping.create(address: "ping")

    ## Subscribe to response topic
    subscribe("ping_topic", "ping")

    ## Create local publisher worker to forward to pong_topic and add metadata to
    ## messages to send responses to ping_topic
    {:ok, address} = init_publisher("pong_topic", "ping_topic")

    ## Send a message THROUGH the local publisher to the remote worker
    send_message([address, "pong"])
  end

  def init_publisher(publisher_stream, consumer_stream) do
    BiDirectional.ensure_publisher(
      consumer_stream,
      publisher_stream,
      stream_options()
    )
  end

  def send_message(onward_route) do
    msg = %Ockam.Message{
      onward_route: onward_route,
      return_route: ["ping"],
      payload: "0"
    }

    Ockam.Router.route(msg)
  end

  def ensure_tcp(port) do
    Ockam.Transport.TCP.create_listener(port: port, route_outgoing: true)
  end

  def subscribe(stream, subscription_id) do
    ## Local subscribe
    ## Create bidirectional subscription on local node
    ## using stream service configuration from stream_options
    BiDirectional.subscribe(stream, subscription_id, stream_options())

    ## This is necessary to make sure we don't spawn publisher for each message
    PublisherRegistry.start_link([])
  end
end
