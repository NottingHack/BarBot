$script = <<SCRIPT

echo "deb http://httpredir.debian.org/debian jessie-backports main" >> /etc/apt/sources.list 
apt-get update
apt-get install -y golang build-essential git

mkdir /home/vagrant/gocode
echo "export GOPATH=/home/vagrant/gocode" >> /home/vagrant/.bashrc

export GOPATH=/home/vagrant/gocode

chmod a+rw /dev/ttyS0

sed -i -e '$i \chmod a+rw /dev/ttyS0\n' /etc/rc.local

cd /vagrant/src/web
go get
go build bb1.go

SCRIPT

# Vagrantfile API/syntax version. Don't touch unless you know what you're doing!
VAGRANTFILE_API_VERSION = "2"

Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  # Every Vagrant virtual environment requires a box to build off of.
  config.vm.box = "debian/contrib-jessie64"
  config.vm.provision "shell", inline: $script
  config.vm.network "forwarded_port", guest: 8080, host: 8080
  
  config.vm.provider :virtualbox do |vb|
    vb.customize ["modifyvm", :id, "--uart1", "0x3f8", "4"]
    vb.customize ["modifyvm", :id, "--uartmode1", "disconnected"]
  end
end
