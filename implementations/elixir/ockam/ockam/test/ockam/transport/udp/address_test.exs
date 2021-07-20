defmodule Ockam.Transport.UDPAddress.Tests do
  use ExUnit.Case, async: true
  doctest Ockam.Transport.UDPAddress
  alias Ockam.RoutableAddress
  alias Ockam.Transport.UDPAddress

  @udp 2
  @four_thousand_encoded <<160, 15>>
  @localhost_binary <<0, 127, 0, 0, 1>>

  describe "Ockam.Transport.UDPAddress" do
    test "2 is the UDP address type" do
      address = %UDPAddress{ip: {127, 0, 0, 1}, port: 4000}
      assert 2 === RoutableAddress.type(address)
    end

    test "can be serialized and then deserialized back to the original address" do
      address = %UDPAddress{ip: {127, 0, 0, 1}, port: 4000}

      serialized = Ockam.Serializable.serialize(address)
      deserialized = UDPAddress.deserialize(serialized)

      assert address === deserialized
    end

    test "Serializing an address produces expected binary" do
      address = %UDPAddress{ip: {127, 0, 0, 1}, port: 4000}

      assert %{type: @udp, value: <<0, 127, 0, 0, 1, 160, 15>>} ==
               Ockam.Serializable.serialize(address)
    end

    test "Deserializing an address produces expected struct" do
      serialized = [@localhost_binary, @four_thousand_encoded]
      assert %UDPAddress{ip: {127, 0, 0, 1}, port: 4000} == UDPAddress.deserialize(serialized)
    end
  end
end
