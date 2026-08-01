curl_transport_option = get_option('curl_transport')

if not curl_transport_option.disabled()
    curl_transport_required = curl_transport_option.enabled() or (
        curl_transport_option.auto() and get_option('profile') == 'host'
    )
    curl_transport_dep = dependency(
        'libcurl',
        required: curl_transport_required,
    )

    if curl_transport_dep.found()
        curl_transport_available = true
        subdir('curl')
    endif
endif
