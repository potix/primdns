require 'unittest.rb'

test 'Simple query' do
  query 'a.example.com' do
    assert_status 'NOERROR'
    assert_flags 'qr', 'aa'
    assert_answer '192.0.2.10'
    assert_authority 'ns1.example.com', 'ns2.example.com'
    assert_additional '192.0.2.1'
    assert_additional '192.0.2.2'
    assert_additional '2001:db8::2:2'
  end
end

test 'Query A for NS' do
  query 'ns2.example.com' do
    assert_status 'NOERROR'
    assert_flags 'qr', 'aa'
    assert_answer '192.0.2.2'
    assert_authority 'ns1.example.com'
    assert_authority 'ns2.example.com'
    assert_additional '192.0.2.1'
    assert_additional '2001:db8::2:2'
    assert_additional '!192.0.2.2'
  end
end

test 'Query NS' do
  query 'NS example.com' do
    assert_status 'NOERROR'
    assert_flags 'qr', 'aa'
    assert_answer 'ns1.example.com'
    assert_answer 'ns2.example.com'
    assert_additional '192.0.2.1'
    assert_additional '2001:db8::2:2'
    assert_additional '!192.0.2.2'
  end
end

test 'Query MX' do
  query 'MX e.example.com' do
    assert_status 'NOERROR'
    assert_flags 'qr', 'aa'
    assert_answer '10 f.example.com'
    assert_answer '20 g.example.com'
    assert_authority 'ns1.example.com'
    assert_authority 'ns2.example.com'
    assert_additional '192.0.2.1'
    assert_additional '192.0.2.2'
    assert_additional '2001:db8::2:2'
    assert_additional '192.0.2.20', 'f.example.com'
    assert_additional '2001:db8::2:20', 'f.example.com'
    assert_additional '192.0.2.21', 'g.example.com'
    assert_additional '2001:db8::2:21', 'g.example.com'
  end
end

test 'Query nonexistent domain name' do
  query 'nonexistent.example.com' do
    assert_status 'NXDOMAIN'
    assert_flags 'qr', 'aa'
    assert_noanswer
    assert_authority_type 'SOA'
  end
end

test 'Query nonexistent domain name' do
  query 'nonexistent.example.com' do
    assert_status 'NXDOMAIN'
    assert_flags 'qr', 'aa'
    assert_authority_type 'SOA'
  end
end

test 'Query nonexistent resource type' do
  query 'TXT a.example.com' do
    assert_status 'NOERROR'
    assert_flags 'qr', 'aa'
    assert_noanswer
    assert_authority_type 'SOA'
  end
end
